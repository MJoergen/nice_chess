/***************************************************************
 * perftsuite.c: runs a perft suite, such as mchess2's
 * tests/perftsuite.epd, read from the console.
 *
 * Each line is a FEN, then ";D1 N1 ;D2 N2 ..." with the node counts
 * of the depths. The depths up to a maximum are checked: the first
 * argument on the host, or PERFT_MAX_DEPTH. Each line also checks
 * that the FEN that the engine writes back is the one it read.
 *
 * It prints a line for each position that fails, and a summary.
 * main returns the number of failures.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

#ifndef PERFT_MAX_DEPTH
#define PERFT_MAX_DEPTH 3
#endif

int main(int argc, char **argv)
{
    int max_depth = (argc > 1) ? atoi(argv[1]) : PERFT_MAX_DEPTH;
    int passed = 0, failed = 0, line_no = 0;
    unsigned long nodes = 0;
    char line[256];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line_no++;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        const char *p;
        if (engine_set_fen(line, &p))
        {
            printf("line %d: invalid FEN\n", line_no);
            failed++;
            continue;
        }

        int ok = 1;

        // The FEN written back, against the one read, without trailing spaces
        char fen[ENGINE_FEN_MAX];
        engine_get_fen(fen);
        size_t len = (size_t) (p - line);
        while (len > 0 && line[len - 1] == ' ')
            len--;
        if (strlen(fen) != len || strncmp(fen, line, len) != 0)
        {
            printf("line %d: FEN written back as %s\n", line_no, fen);
            ok = 0;
        }

        while ((p = strchr(p, ';')) != NULL)
        {
            p++;
            if (*p != 'D')
                continue;
            int depth = atoi(p + 1);
            while (*p && *p != ' ')
                p++;
            unsigned long expected = strtoul(p, NULL, 10);
            if (depth > max_depth)
                break;
            unsigned long got = engine_perft(depth);
            nodes += got;
            if (got != expected)
            {
                printf("line %d: depth %d: %lu, expected %lu\n", line_no, depth, got, expected);
                ok = 0;
                break;
            }
        }

        if (ok)
            passed++;
        else
            failed++;
    }

    printf("perft suite to depth %d: %d passed, %d failed, %lu nodes\n",
            max_depth, passed, failed, nodes);
    return failed;
}
