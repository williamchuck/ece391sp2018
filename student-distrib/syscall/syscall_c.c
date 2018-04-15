#include "syscall.h"
#include "../fs/fs.h"
#include "../page/page.h"
#include "../lib.h"
#include "../x86_desc.h"
#include "../process/process.h"
#include "../drivers/stdin.h"
#include "../drivers/stdout.h"

/*
 * get_free_pid:
 * Description: Get a free pid for child process
 * Input: None
 * Output: pid for child process
 * Effect: None
 */
int32_t get_free_pid(){
	int i;
	for(i = 0; i < 6; i++){
		if(process_desc_arr[i].flag == 0){
			//process_desc_arr[i].flag = 1;
			return i;
		}
	}

	return -1;
}

/*
 * system_execute:
 * Description: Execute system call kernel function
 * Input: File name of the file want to execute
 * Output: -1 on fail, 0 on success
 * Effect: Execute the file
 */
int32_t system_execute(const int8_t* file_name){
    int fd, size, child_pid, i, j;
    void* entry_addr;
    uint32_t child_kernel_ESP, user_ESP, phys_addr, virt_addr;
		int8_t command[BUF_SIZE];
		int8_t cmd_started;

    /* Get pid for child */
    child_pid = get_free_pid();
    /* Check if pid is valid */
    if(child_pid == -1){
        return -1;
    }

		/* Copy filename to shell buffer */
		/* Also clears command var. */
		for (i = 0; i < BUF_SIZE; i++)
		{
			shell_buf[i] = file_name[i];
			command[i] = 0x00;
		}

		/* Extract actual command from entire shell line. */
		cmd_started = 0;
		j = 0;

		for (i = 0; i < BUF_SIZE; i++)
		{
			if (file_name[i] == 0x00)
			{
				break;
			}

			if (file_name[i] != ASCII_SPACE)
			{
				if (cmd_started == 0)
				{
					cmd_started = 1;
				}
				command[j] = file_name[i];
				j++;
			}
			else if ((file_name[i] == ASCII_SPACE) && (cmd_started == 1))
			{
				break;
			}
		}

    /* open file */
    fd = system_open(command);
    if(fd == -1)
        return -1;
    //check that it's a data file
    if(current_PCB->file_desc_arr[fd].f_op != data_op){
        system_close(fd);
        return -1;
    }
    /* Get file size before switching page since the string wouldn't be accessible once page is switched */
    size = get_size(command);
    if(size<ENTRY_POS_OFFSET){//if the file didn't reach the size where entry location is recorded, then it's not executable
        system_close(fd);
        return -1;
    }

    /* Set up correct memory mapping for child process */
    phys_addr = _8MB + (child_pid * _4MB);
    virt_addr = _128MB;
    set_4MB(phys_addr, virt_addr, 3);

    /* Load user program */
    uint8_t* buf=(uint8_t*)(virt_addr + OFFSET);
    system_read(fd, buf, size);
    //close file as we have finished loading the contents
    system_close(fd);

    /* Check if file is executable */
    if( *((uint32_t*)buf) != EXE_MAGIC){
	    /* Remap memory back to current process */
        set_4MB(_8MB + (current_PCB->pid * _4MB), virt_addr, 3);
        return -1;
    }

    /* Mark child pid as used */
    process_desc_arr[child_pid].flag = 1;

    /* Get the entry addr:
     * get address of buf[ENTRY_POS_OFFSET],
     * reinterpret that as a double pointer,
     * dereference it to get entry addr */
    entry_addr = *((void**)(&(buf[ENTRY_POS_OFFSET])));

    /* Set up user esp at the bottom of the user page, -8 so it doesnt go over page limit */
    user_ESP = virt_addr + _4MB - 8;
    /* Set up child process kernel esp at the bottom of the 8KB block */
    child_kernel_ESP = _8MB - (child_pid * _8KB) - 8;

    /* Set TSS esp0 to child process kernel stack */
    tss.esp0 = child_kernel_ESP;

	/* Calculate location of child PCB
	 * top of the 8KB child kernel stack region is for PCB
	 * as outlined in get_current_PCB */
	PCB_block_t* child_PCB;
	child_PCB = (PCB_block_t*)(child_kernel_ESP - _8KB + 8);

	/* Initialize file_desc_arr for child process */
	for(i = 0; i < 8; i++)
		child_PCB->file_desc_arr[i].flag = 0;

	/* Initialize child process control block */
	child_PCB->file_desc_arr[0].f_op = stdin_op;
	child_PCB->file_desc_arr[0].inode = 0;
	child_PCB->file_desc_arr[0].f_pos = 0;
	child_PCB->file_desc_arr[0].flag = 1;

	child_PCB->file_desc_arr[1].f_op = stdout_op;
	child_PCB->file_desc_arr[1].inode = 0;
	child_PCB->file_desc_arr[1].f_pos = 0;
	child_PCB->file_desc_arr[1].flag = 1;

	child_PCB->parent_PCB = current_PCB;
	child_PCB->pid = child_pid;

	/* Jump to user space */
	return jump_to_user(entry_addr, (uint32_t*)user_ESP, &(child_PCB->halt_back_ESP));
}

/*
 * system_halt:
 * Description: Halt system call kernel function
 * Input: Return value to user space
 * Output: Always -1 because it should not return
 * Effect: Halt the file
 */
int32_t system_halt(uint8_t status){
	uint32_t pid, phys_addr, virt_addr, parent_pid;
	pid = current_PCB->pid;

	/* Unset flag for current pid */
	process_desc_arr[pid].flag = 0;

	/* Get parent pid */
	parent_pid = current_PCB->parent_PCB->pid;

	/* Get parent's userspace mem addr to remap */
	phys_addr = _8MB + (parent_pid * _4MB);
	virt_addr = _128MB;
	set_4MB(phys_addr, virt_addr, 3);

	/* Set up parent kernel stack addr for TSS */
	tss.esp0 = (uint32_t)(_8MB - (parent_pid * _8KB)-8);

	/* Jump to return from execute */
	halt_ret_exec(current_PCB->halt_back_ESP, status);

	/* Dummy return */
	return -1;
}

/*
 * system_open:
 * Description: Open system call kernel function
 * Input: File name of the file to return
 * Output: -1 on fail, file descriptor on success
 * Effect: Open the file
 */
int32_t system_open(const int8_t* fname){
	dentry_t dentry;
	file_desc_t file;

	/* Get dentry of the file */
	read_dentry_by_name(fname, &dentry);

	/* Assign correct file operations */
	if(dentry.file_type == 0)
		file.f_op = rtc_op;
	else if(dentry.file_type == 1)
		file.f_op = dir_op;
	else if(dentry.file_type == 2)
		file.f_op = data_op;
	else
		return -1;

	/* Call corresponding open serivice call */
	return file.f_op->open(fname);
}

/*
 * system_read:
 * Description: Read system call kernel function
 * Input: File descirptor of the file to read, a pointer to a buffer to read into
 * 	  and the number of bytes to read
 * Output: -1 on fail, number of bytes actually read on success
 * Effect: Read the file
 */
int32_t system_read(int32_t fd, void* buf, uint32_t size){
    //fd range check
    if(fd<FD_MIN||fd>FD_MAX)return -1;
    //check if fd loaded
    if(!current_PCB->file_desc_arr[fd].flag)return -1;
	/* Call corresponding read serivice call */
	return current_PCB->file_desc_arr[fd].f_op->read(fd, buf, size);
}

/*
 * system_write:
 * Description: Write system call kernel function
 * Input: File descirptor of the file to write, a pointer to a buffer to write from
 * 	  and the number of bytes to write
 * Output: -1 on fail, number of bytes actually write on success
 * Effect: Write the file
 */
int32_t system_write(int32_t fd, const void* buf, uint32_t size){
    //fd range check
    if(fd<FD_MIN||fd>FD_MAX)return -1;
    //check if fd loaded
    if(!current_PCB->file_desc_arr[fd].flag)return -1;
	/* Call corresponding write serivice call */
	return current_PCB->file_desc_arr[fd].f_op->write(fd, buf, size);
}

/*
 * system_close:
 * Description: Close system call kernel function
 * Input: File descriptor of the file to close
 * Output: -1 on fail, 0 on success
 * Effect: Close the file
 */
int32_t system_close(int32_t fd){
    //fd range check
    if(fd<FD_MIN||fd>FD_MAX)return -1;
    //check if fd loaded
    if(!current_PCB->file_desc_arr[fd].flag)return -1;
    //stdin and out can't be closed
    if(fd==0||fd==1)return -1;
	/* Call corresponding close serivice call */
	return current_PCB->file_desc_arr[fd].f_op->close(fd);
}

/*
 * system_getargs
 * DESCRIPTION: get arguments in a comman line
 * INPUTS: buf - target buffer to be copied to.
 * 				 nbytes - number of bytes in target buffer
 * OUTPUT: none.
 * RETURN VALUE: -1 when failed. number of bytes copied when success.
 * SIDE EFFECTS: none.
 */
int32_t system_getargs(uint8_t* buf, int32_t nbytes)
{
	int arg_startindex;
	int arg_finishindex;
	int i;
	uint8_t cmd_started;
	uint8_t cmd_finished;
	uint8_t arg_started;
	uint8_t arg_finished;

	cmd_started = 0;
	cmd_finished = 0;
	arg_started = 0;
	arg_finished = 0;
	arg_startindex = -1;
	arg_finishindex = -1;

	for (i = 0; i < BUF_SIZE; i++)
	{
		if ((cmd_started == 0) && shell_buf[i] != ASCII_SPACE)
		{
			cmd_started = 1;
			continue;
		}
		if ((cmd_started == 1) && (cmd_finished == 0) && (shell_buf[i] == ASCII_SPACE))
		{
			cmd_finished = 1;
			continue;
		}
		if ((cmd_finished == 1) && (arg_started == 0) && (shell_buf[i] != ASCII_SPACE))
		{
			arg_started = 1;
			arg_startindex = i;
			continue;
		}
		if (shell_buf[i] == 0x00)
		{
			arg_finished = 1;
			arg_finishindex = i;
			break;
		}
	}

	if ((arg_startindex == -1) || arg_finishindex < arg_startindex)
	{
		return -1;
	}

	if (arg_finishindex - arg_startindex + 1 > nbytes)
	{
		return -1;
	}

	for (i = 0; i < nbytes; i++)
	{
		buf[i] = 0x00;
	}

	for (i = arg_startindex; i <= arg_finishindex; i++)
	{
		buf[i - arg_startindex] = shell_buf[i];
	}

	return 0;
}
