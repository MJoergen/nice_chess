/***************************************************************
 * search.c: the search, and perft.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

// Scores are in centipawns. A mate is worth far more than any material.
#define MATE 30000
#define INF  32000

/***************************************************************
 * quiescence
 *
 * Searches only captures, until the position is quiet. This avoids
 * evaluating positions in the middle of an exchange (the horizon
 * effect). The side to move may always decline to capture
 * ("stand pat").
 ***************************************************************/
static int quiescence(int alpha, int beta)
{
    int best = evaluate();
    if (best >= beta || !move_stack_room())
        return best;
    if (best > alpha)
        alpha = best;

    move_t *list = move_stack + move_sp;
    int n = gen_pseudo(list);
    move_sp += n;

    for (int i = 0; i < n; ++i)
    {
        move_t m = list[i];
        if (move_captured(m) == EM)
            continue;

        make_move(m);
        if (!move_was_legal())
        {
            undo_move();
            continue;
        }
        int val = -quiescence(-beta, -alpha);
        undo_move();

        if (val > best)
            best = val;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;
    }

    move_sp -= n;
    return best;
} // end of quiescence

/***************************************************************
 * search
 *
 * A fail-soft negamax alpha-beta search of depth ply, followed by
 * the quiescence search. The value is from the point of view of
 * the side to move: -(MATE + depth) if it is mated, so that a
 * nearer mate counts more, and 0 if it is stalemated.
 ***************************************************************/
static int search(int alpha, int beta, int depth)
{
    if (depth == 0 || !move_stack_room())
        return quiescence(alpha, beta);

    move_t *list = move_stack + move_sp;
    int n = gen_pseudo(list);
    move_sp += n;

    int best = -INF;
    int any_legal = 0;

    for (int i = 0; i < n; ++i)
    {
        make_move(list[i]);
        if (!move_was_legal())
        {
            undo_move();
            continue;
        }
        any_legal = 1;
        int val = -search(-beta, -alpha, depth - 1);
        undo_move();

        if (val > best)
            best = val;
        if (best > alpha)
            alpha = best;
        if (alpha >= beta)
            break;
    }

    move_sp -= n;

    if (!any_legal)
        return is_attacked(king_of(pos.side), pos.side) ? -(MATE + depth) : 0;
    return best;
} // end of search

/***************************************************************
 * engine_search
 *
 * Searches each legal move to depth - 1 with a full window, and
 * returns the first of the best.
 ***************************************************************/
move_t engine_search(int depth, engine_info_fn info)
{
    move_t *list = move_stack + move_sp;
    int n = gen_legal(list);
    move_sp += n;

    move_t best_move = 0;
    int best = -INF;
    for (int i = 0; i < n; ++i)
    {
        make_move(list[i]);
        int val = -search(-INF, INF, depth > 0 ? depth - 1 : 0);
        undo_move();
        if (info)
            info(list[i], val);
        if (val > best)
        {
            best = val;
            best_move = list[i];
        }
    }

    move_sp -= n;
    return best_move;
} // end of engine_search

/***************************************************************
 * perft
 * At depth 1 it counts the legal moves, without making them twice.
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
#ifdef CHECK_HASH
            assert(pos.hash == board_compute_hash());
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
