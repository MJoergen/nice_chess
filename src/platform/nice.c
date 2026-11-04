/***************************************************************
 * nice.c: the platform, on NICE: niceemu, or a board such as the
 * MEGA65 under NiceMonMEGA65's monitor.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <nice.h>
#include "platform.h"

uint32_t platform_micros(void)
{
    return (uint32_t) nice_micros();
}

// In niceemu the cycle counter and the microsecond counter both count
// steps (nice_cpu's SYSTEM.md §3), also with --window, where a step
// takes a microsecond. On a board both count real time, the cycles at
// the clock's rate, 100 MHz on the MEGA65. So the ratio of the two
// over a millisecond tells them apart, once, at the first call.
int platform_on_board(void)
{
    static int known, board;
    if (!known)
    {
        uint32_t c0 = (uint32_t) nice_cycles();
        uint32_t u0 = (uint32_t) nice_micros();
        while ((uint32_t) nice_micros() - u0 < 1000)
            ;
        uint32_t cycles = (uint32_t) nice_cycles() - c0;
        uint32_t us = (uint32_t) nice_micros() - u0;
        board = cycles / us >= 10;
        known = 1;
    }
    return board;
}

// 300 million steps in niceemu, about 7 seconds there and 5.5 on the
// MEGA65, which runs about 54 million steps a second of this program.
// On a board, where the clock is real, 5 seconds.
uint32_t platform_move_time(void)
{
    return platform_on_board() ? 5000000 : 300000000;
}

// On a board, the clock and the cycle counter at the first call, which
// the time the player took to load and start the program sets.
uint32_t platform_seed(void)
{
    return platform_on_board() ? (uint32_t) nice_micros() ^ (uint32_t) nice_cycles() : 1;
}

int platform_pollc(void)
{
    return nice_pollc();
}
