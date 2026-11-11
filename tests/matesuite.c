/***************************************************************
 * matesuite.c: plays out won end games, read from the console, and
 * checks that the engine mates.
 *
 * Each line is a FEN in which one side has only its king. The other
 * side, the engine at each number of nodes of the arguments, plays
 * against the bare king, the engine at DEFENDER_NODES, until the game
 * ends or MAX_MOVES moves of the stronger side have been played.
 *
 * It prints, for each number of nodes, how many of the positions it
 * mated within MAX_MOVES moves, and then the longest mate. With -v,
 * it prints each game's result.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

#define DEFENDER_NODES 2000
#define MAX_MOVES      50
#define MAX_POSITIONS  64

// 1 if white has only its king, -1 if black has, 0 if neither.
static int bare_side(void)
{
    int white = 0, black = 0;
    for (int f = 0; f < 8; ++f)
        for (int r = 0; r < 8; ++r)
        {
            int p = engine_piece_at(f, r);
            white += p > 0;
            black += p < 0;
        }
    return white == 1 ? 1 : black == 1 ? -1 : 0;
}

// Plays the game from fen, and returns the number of moves of the
// stronger side to mate, or 0 if it did not mate.
static int play(const char *fen, uint32_t nodes, int verbose)
{
    engine_new_game();
    engine_set_fen(fen, NULL);
    int strong = -bare_side();
    int moves = 0;
    int result;
    while ((result = engine_result()) == ENGINE_PLAYING && moves < MAX_MOVES)
    {
        int ours = engine_side_to_move() == strong;
        engine_limits_t limits = {0, ours ? nodes : DEFENDER_NODES, NULL};
        move_t m = engine_search(&limits, NULL);
        if (!m || engine_make_move(m))
            break;
        moves += ours;
    }
    int mated = result == ENGINE_CHECKMATE && engine_side_to_move() != strong;
    if (verbose)
        printf("%u nodes: %s %d moves: %s\n", (unsigned) nodes,
               mated ? "mate in" : result == ENGINE_PLAYING ? "no mate after" : "draw after",
               moves, fen);
    return mated ? moves : 0;
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    if (verbose)
        argv++, argc--;

    static char fens[MAX_POSITIONS][128];
    int n = 0;
    char line[128];
    while (n < MAX_POSITIONS && fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;
        engine_new_game();
        if (engine_set_fen(line, NULL) || !bare_side())
        {
            printf("not a bare king: %s\n", line);
            return 1;
        }
        strcpy(fens[n++], line);
    }

    int failed = 0;
    for (int a = 1; a < argc; ++a)
    {
        uint32_t nodes = (uint32_t) strtoul(argv[a], NULL, 10);
        int mated = 0, longest = 0;
        for (int i = 0; i < n; ++i)
        {
            int moves = play(fens[i], nodes, verbose);
            mated += moves > 0;
            if (moves > longest)
                longest = moves;
        }
        printf("mate suite at %u nodes: %d of %d\n", (unsigned) nodes, mated, n);
        printf("  the longest mate: %d moves\n", longest);
        failed |= mated < n;
    }
    return failed;
}
