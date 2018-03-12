/*
 * rtc.c
 *
 *  Created on: Mar 11, 2018
 *      Author: Dennis
 */


#include "rtc.h"
#include "../lib.h"
#include "../idt/interrupt.h"

extern void test_interrupts(void);
/*  rtc_isr
 *  Interrupt service routine for RTC
 *  Side effects: display garbage on screen, clear interrupt on RTC
 */
void rtc_isr(){
    test_interrupts();
    outb(0x0C, 0x70);   //select register C
    inb(0x71);          //read register to clear interrupt
}
/*  rtc_enable_interrupt
 *  Enable RTC periodic interrupt and install the rtc_isr interrupt handler
 */
int rtc_enable_interrupt(){
    //referenced from https://wiki.osdev.org/RTC
    unsigned long flags;
    cli_and_save(flags);
    outb(0x8A, 0x70);   //select register A, disable NMI
    outb(0x2F, 0x71);   //Turn on oscillator, select update rate to be 500ms
    outb(0x8B, 0x70);   //select register B, disable NMI
    char regB=inb(0x71);//store current value
    outb(0x8B, 0x70);   //select register B, disable NMI
    outb(regB | 0x40, 0x71); //set Periodic Interrupt Enable bit and write it back
    outb(0x0B, 0x70);//just to enable NMI
    inb(0x71); //just read
    restore_flags(flags);
    return request_irq(8, &rtc_isr);//request to install handler
}
