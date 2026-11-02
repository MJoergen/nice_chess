/***************************************************************
 * host.c: the platform, on a PC.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <poll.h>
#include <time.h>
#include <unistd.h>
#include "platform.h"

uint32_t platform_micros(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ts.tv_sec * 1000000u + (uint32_t) (ts.tv_nsec / 1000);
}

uint32_t platform_move_time(void)
{
    return 1000000; // 1 second
}

int platform_on_board(void)
{
    return 0;
}

int platform_pollc(void)
{
    struct pollfd p = {0, POLLIN, 0};
    if (poll(&p, 1, 0) <= 0)
        return -2;
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}
