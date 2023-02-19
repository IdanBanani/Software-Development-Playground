#ifndef _KEYBOARD_LISTENER
#define _KEYBOARD_LISTENER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include "client.h"
#include "../common/common.h"
#define CLEAR_SCREEN() printf("\033[1;1H\033[2J")

void *start_keyboard_listener(void *args);

// '\x1b[2J\x1b[H',
// printf("\033c");

//cfmakeraw:
/*
Control:
DON'T Echo input characters.
DON'T Echo new line?
NOT Canonical mode
don't strip the eight bit
INLCR don't Translate NL to CR on input.
Don't Ignore carriage return on input.
dont Translate carriage return to newline on input (unless IGNCR is set).
BLOCKS INTR, QUIT, SUSP, or DSUSP
Disable  implementation-defined input processing. 
disable  implementation-defined output processing.
char size is 8Byte
Don't generate parity and don't check
*/

void reset_terminal_mode();

void set_conio_terminal_mode();
int kbhit();

int getch();
void *start_keyboard_listener(void *args);

#endif