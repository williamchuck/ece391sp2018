/*
 * ps2_keyboard.h
 * Definitions of keyboard initialization and interrupt handling.
 * Author: Canlin Zhang
 */
#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

 /* Include PIC, library, stdint and interrupt. */
#include "../i8259.h"
#include "../lib.h"
#include "../types.h"
#include "../idt/interrupt.h"

/* Include standard I/O */
#include "stdin.h"
#include "stdout.h"

/* Keyboard IRQ number = 1 */
#define KBD_IRQ     0x01

/* Size of buffer is 128 */
#define BUF_SIZE    128

/* Initialize keyboard */
void ps2_keyboard_init();

/* Get Raw scan code from keyboard */
unsigned char ps2_keyboard_getscancode();

/* Get converted ASCII code, given scan code. */
unsigned char ps2_keyboard_getchar(unsigned char scancode);

/* Process flags generated by ctrl, alt, shift, and lock keys */
void ps2_keyboard_processflags(unsigned char scancode);

/* Interrupt handler for keyboard */
extern void int_ps2kbd_c();

/* variables for current keycode and ascii (if applicable) */
unsigned char currentcode;
unsigned char currentchar;

#endif
