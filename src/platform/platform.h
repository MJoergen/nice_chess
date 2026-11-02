/***************************************************************
 * platform.h: what a front end needs from the machine it runs on,
 * beyond the C library. nice.c is for NICE, and host.c for a PC.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// A clock in microseconds. It wraps after 71 minutes, so only the
// difference of two readings means anything, as uint32_t.
//
// In niceemu, a microsecond is one step (one instruction), which the
// pipelined core on the MEGA65 runs in about 1/70 of a microsecond.
uint32_t platform_micros(void);

// The time a move takes by default, in microseconds of
// platform_micros.
extern const uint32_t platform_move_time;

// The next byte of the console's input, if one waits: 0 to 255, or
// -2 if none waits yet, or -1 once the input has ended. From a file,
// or from stdin in niceemu, it waits for the next byte, and never
// returns -2. From a keyboard (niceemu's window, or the board's UART
// and keyboard) it does not wait.
int platform_pollc(void);

#endif // PLATFORM_H
