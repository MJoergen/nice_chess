/***************************************************************
 * searchsuite.c: runs a suite of test positions, such as mchess2's
 * tests/searchsuite.epd, read from the console.
 *
 * Each line is an EPD: a FEN of four fields (or six), then
 * operations, such as
 *   bm Qd1+; id "BK.01";
 * The engine searches each position to a fixed number of nodes, the
 * first argument, and solves it if its move is one of the best moves
 * (bm), or is none of the moves to avoid (am). The moves are in SAN,
 * which this file writes for the engine's moves, as the engine has
 * none.
 *
 * It prints, for each group of positions (the id up to its first
 * '.' or digit), how many it solved, and the total. With -v, it
 * prints each position that it did not solve.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

static const char piece_letters[] = " PNBRQK"; // By piece, of either side

static int iabs(int x) { return x < 0 ? -x : x; }

/***************************************************************
 * to_san
 * Writes a legal move in SAN, without + or #, to buf.
 ***************************************************************/
static void to_san(move_t m, const move_t *legal, int n, char *buf)
{
    char s[8], t[8];
    engine_move_to_str(m, s);
    int ff = s[0] - 'a', fr = s[1] - '1', tf = s[2] - 'a', tr = s[3] - '1';
    int piece = iabs(engine_piece_at(ff, fr));
    int capture = engine_piece_at(tf, tr) != EM || (piece == WP && ff != tf);

    if (piece == WK && iabs(tf - ff) == 2)
    {
        strcpy(buf, tf > ff ? "O-O" : "O-O-O");
        return;
    }

    char *p = buf;
    if (piece == WP)
    {
        if (capture)
            *p++ = s[0];
    }
    else
    {
        *p++ = piece_letters[piece];
        // The other moves of the same kind of piece to the same square
        int same_file = 0, same_rank = 0, others = 0;
        for (int i = 0; i < n; ++i)
        {
            engine_move_to_str(legal[i], t);
            if (legal[i] == m || strncmp(t + 2, s + 2, 2) != 0)
                continue;
            if (iabs(engine_piece_at(t[0] - 'a', t[1] - '1')) != piece)
                continue;
            others++;
            same_file |= t[0] == s[0];
            same_rank |= t[1] == s[1];
        }
        if (others && (!same_file || same_rank))
            *p++ = s[0];
        if (others && same_file)
            *p++ = s[1];
    }
    if (capture)
        *p++ = 'x';
    *p++ = s[2];
    *p++ = s[3];
    if (s[4])
    {
        *p++ = '=';
        *p++ = (char) toupper(s[4]);
    }
    *p = '\0';
}

// True if the SAN move a, without + # ! ?, is b.
static int san_equal(const char *a, int len, const char *b)
{
    while (len > 0 && strchr("+#!?", a[len - 1]))
        len--;
    return (int) strlen(b) == len && strncmp(a, b, (size_t) len) == 0;
}

// True if the list of SAN moves in ops after the word op (up to ';')
// has san.
static int op_has(const char *ops, const char *op, const char *san, int *found)
{
    size_t oplen = strlen(op);
    const char *p = ops;
    while ((p = strstr(p, op)) != NULL)
    {
        if ((p == ops || p[-1] == ' ' || p[-1] == ';') && p[oplen] == ' ')
            break;
        p++;
    }
    *found = p != NULL;
    if (!p)
        return 0;
    p += oplen;
    while (*p && *p != ';')
    {
        while (*p == ' ')
            p++;
        const char *w = p;
        while (*p && *p != ' ' && *p != ';')
            p++;
        if (p > w && san_equal(w, (int) (p - w), san))
            return 1;
    }
    return 0;
}

#define MAX_GROUPS 16

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    if (verbose)
        argv++, argc--;
    uint32_t max_nodes = argc > 1 ? (uint32_t) strtoul(argv[1], NULL, 10) : 10000;

    char group_name[MAX_GROUPS][16];
    int group_solved[MAX_GROUPS], group_total[MAX_GROUPS], groups = 0;
    int solved = 0, total = 0;
    char line[512];

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        const char *ops;
        engine_new_game();
        if (engine_set_fen(line, &ops))
        {
            printf("invalid FEN: %s\n", line);
            continue;
        }

        // The group, from the id
        char name[16] = "?";
        const char *id = strstr(ops, "id \"");
        if (id)
        {
            id += 4;
            int k = 0;
            while (k < 15 && id[k] && id[k] != '"' && id[k] != '.' && !isdigit((unsigned char) id[k]))
                name[k] = id[k], k++;
            while (k > 0 && name[k - 1] == ' ')
                k--;
            name[k] = '\0';
        }
        int g = 0;
        while (g < groups && strcmp(group_name[g], name))
            g++;
        if (g == groups && groups < MAX_GROUPS)
        {
            strcpy(group_name[groups], name);
            group_solved[groups] = group_total[groups] = 0;
            groups++;
        }

        engine_limits_t limits = {0, max_nodes, NULL};
        move_t m = engine_search(&limits, NULL);
        move_t legal[ENGINE_MAX_MOVES];
        int n = engine_legal_moves(legal);
        char san[16] = "none";
        if (m)
            to_san(m, legal, n, san);

        int has_bm, has_am;
        int ok = op_has(ops, "bm", san, &has_bm);
        if (!has_bm)
        {
            ok = !op_has(ops, "am", san, &has_am);
            if (!has_am)
                continue; // Nothing to check
        }

        total++;
        solved += ok;
        if (g < MAX_GROUPS)
        {
            group_total[g]++;
            group_solved[g] += ok;
        }
        if (verbose && !ok)
            printf("not solved, played %s: %s\n", san, line);
    }

    for (int g = 0; g < groups; ++g)
        printf("search suite %s: %d of %d\n", group_name[g], group_solved[g], group_total[g]);
    printf("search suite at %u nodes: %d of %d\n", (unsigned) max_nodes, solved, total);
    return 0;
}
