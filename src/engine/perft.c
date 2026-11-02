/***************************************************************
 * perft.c: perft, the count of the leaves of the tree of legal
 * moves, which tests the move generator and make and undo.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

/***************************************************************
 * perft
 * At depth 1 it counts the legal moves, without making them twice.
 * With CHECK_STATE, it checks at every node that make_move kept
 * the Zobrist key and the evaluation's sums right.
 ***************************************************************/
static uint32_t perft(int depth)
{
    assert(move_stack_room());
    move_t *list = move_stack + move_sp;
    int n = gen_pseudo(list);
    move_sp += n;

    uint32_t count = 0;
    for (int i = 0; i < n; ++i)
    {
        make_move(list[i]);
        if (move_was_legal())
        {
#ifdef CHECK_STATE
            assert(board_consistent());
#endif
            count += (depth > 1) ? perft(depth - 1) : 1;
        }
        undo_move();
    }

    move_sp -= n;
    return count;
} // end of perft

uint32_t engine_perft(int depth)
{
    if (depth <= 0)
        return 1;
    return perft(depth);
} // end of engine_perft
