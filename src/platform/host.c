/***************************************************************
 * host.c: the platform, on a PC.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <time.h>
#include "platform.h"

uint32_t platform_micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ts.tv_sec * 1000000u + (uint32_t) (ts.tv_nsec / 1000);
}

const uint32_t platform_move_time = 1000000; // 1 second
