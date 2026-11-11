/***************************************************************
 * perft.c: perft, the count of the leaves of the tree of legal
 * moves, which tests the move generator and make and undo.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

#ifdef CHECK_STATE
/***************************************************************
 * captures_consistent
 * True if gen_captures gives the moves of list that capture or
 * promote, and in the same order. list is all the pseudo-legal
 * moves, n of them, at the top of the move stack.
 ***************************************************************/
static int captures_consistent(const move_t *list, int n)
{
    assert(move_stack_room());
    move_t *caps = move_stack + move_sp;
    int c = gen_captures(caps);
    int k = 0;

    for (int i = 0; i < n; ++i)
    {
        if (move_captured(list[i]) == EM && move_promoted(list[i]) == EM)
            continue;
        if (k >= c || caps[k] != list[i])
            return 0;
        ++k;
    }
    return k == c;
} // end of captures_consistent
#endif

/***************************************************************
 * perft
 * At depth 1 it counts the legal moves, without making them twice.
 * With CHECK_STATE, it checks at every node that make_move kept
 * the Zobrist key and the evaluation's sums right, and that
 * gen_captures gives the captures and promotions of the full list.
 ***************************************************************/
static uint32_t perft(int depth)
{
    assert(move_stack_room());
    move_t *list = move_stack + move_sp;
    int n = gen_pseudo(list);
    move_sp += n;
#ifdef CHECK_STATE
    assert(captures_consistent(list, n));
#endif

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
