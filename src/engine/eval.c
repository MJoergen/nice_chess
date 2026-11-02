/***************************************************************
 * eval.c: the evaluation of a position.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

/***************************************************************
 * evaluate
 *
 * Returns the value of the position in centipawns, from the point
 * of view of the side to move. As in mchess.c, it counts two things:
 * 1. The material: pawn 100, knight 300, bishop 300, rook 500, and
 *    queen 900.
 * 2. The difference in the number of pseudo-legal moves of the two
 *    sides, which favors development and centralization.
 *
 * The second part generates the moves of both sides at every leaf,
 * and so costs most of the time of the search. Phase 2 replaces it
 * (doc/PORTING_PLAN.md).
 ***************************************************************/
int evaluate(void)
{
    move_t *list = move_stack + move_sp;
    if (!move_stack_room())
        return pos.side * pos.material;

    int mine = gen_pseudo(list);

    // The opponent's moves, from this position. The en passant square
    // belongs to the side to move, so it is hidden meanwhile.
    int ep = pos.ep;
    pos.ep = 0;
    pos.side = -pos.side;
    int theirs = gen_pseudo(list);
    pos.side = -pos.side;
    pos.ep = ep;

    return (mine - theirs) + pos.side * pos.material;
} // end of evaluate
