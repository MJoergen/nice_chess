/***************************************************************
 * nice.c: the platform, on NICE.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <nice.h>
#include "platform.h"

uint32_t platform_micros(void)
{
    return (uint32_t) nice_micros();
}

// 300 million steps in niceemu, which is about 4 seconds on the
// board. On the board itself, where the clock is real, this would be
// 5 minutes; Phase 4 sets the time for the board.
const uint32_t platform_move_time = 300000000;

int platform_pollc(void)
{
    return nice_pollc();
}
