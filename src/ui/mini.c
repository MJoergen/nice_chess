/***************************************************************
 * mini.c: the minimal front end, on the console. It reads a line,
 * runs it as a command of miniuci.c, and writes the reply. It is the
 * build for the Nexys4DDR, whose monitor gives a program 26K words.
 *
 * A line ends at LF or CR, as a terminal on the board's line sends
 * CR, and a file LF; an LF right after a CR is the same end.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "../platform/platform.h"
#include "miniuci.h"

// position startpos moves ... of the longest game the engine keeps:
// 448 plies of up to 6 characters.
#define LINE_MAX 2800

static char line[LINE_MAX];

int main(void)
{
    int last = 0; // The byte before
    mini_init(platform_seed());
    while (1)
    {
        int n = 0, c;
        while ((c = platform_getc()) >= 0)
        {
            int prev = last;
            last = c;
            if (c == '\n' && prev == '\r')
                continue;
            if (c == '\n' || c == '\r')
                break;
            if (n < LINE_MAX - 1)
                line[n++] = (char) c;
        }
        if (c < 0 && n == 0)
            return 0;
        line[n] = '\0';

        const char *r = mini_command(line);
        if (!r)
            return 0;
        while (*r)
            platform_putc(*r++);
        if (c < 0)
            return 0;
    }
}
