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

#endif // PLATFORM_H
