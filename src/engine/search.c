/***************************************************************
 * search.c: the search.
 *
 * A negamax alpha-beta search, fail-soft, by iterative deepening,
 * as a principal variation search: the first move of a node with the
 * full window, and the others with a null window, searched again
 * with the full window if one turns out better. It has a quiescence
 * search of captures at the leaves, a check extension, the
 * transposition table, null-move pruning, and late move reductions.
 * The moves are tried in this order: the move of the transposition
 * table, the captures by MVV-LVA (the most valuable victim first,
 * and of those, the least valuable attacker first), promotions to a
 * queen, the two killer moves of the ply, and then the rest by their
 * history: how often, and how deep, the same piece to the same
 * square has cut a node before.
 *
 * Draws by repetition and by the 50-move rule are scored 0 inside
 * the tree. A position that has come up once before counts as a
 * draw there, as a repetition is in the hands of either side.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

#define INF 32000

// Null-move pruning: the depth that the null move takes off, besides
// its own ply, and the least depth at which it is tried.
#ifndef NULL_R
#define NULL_R 2
#endif
#ifndef NULL_MIN_DEPTH
#define NULL_MIN_DEPTH 2
#endif

// Late move reductions: a quiet move after the first LMR_MIN_MOVES
// moves of a node, at depth LMR_MIN_DEPTH or more, is searched
// LMR_R plies shallower, and again at the full depth if it turns out
// better than alpha.
#ifndef LMR_R
#define LMR_R 1
#endif
#ifndef LMR_MIN_DEPTH
#define LMR_MIN_DEPTH 3
#endif
#ifndef LMR_MIN_MOVES
#define LMR_MIN_MOVES 3
#endif

// The history of a quiet move is at most this, below ORDER_KILLER2.
#define HISTORY_MAX (1 << 16)

// The order of the moves of a list, by the score of each.
enum
{
    ORDER_TT      = 1 << 30,
    ORDER_CAPTURE = 1 << 20,  // Plus MVV-LVA
    ORDER_QUEEN   = 1 << 19,  // A promotion to a queen
    ORDER_KILLER1 = 1 << 18,
    ORDER_KILLER2 = 1 << 17
};

static int32_t order[MOVE_STACK_SIZE];         // Beside move_stack
static move_t  killers[ENGINE_MAX_PLY][2];
static int32_t history[13][64];                // By piece + 6 and sq64 of to

// The principal variation of each ply: pv_table[ply] is the best line
// found so far from the node at ply, of pv_length[ply] moves.
static move_t  pv_table[ENGINE_MAX_PLY][ENGINE_MAX_PV];
static int32_t pv_length[ENGINE_MAX_PLY];

static engine_limits_t limits;
static uint32_t nodes;
static int      stopped;

static inline int iabs(int x) { return x < 0 ? -x : x; }

/***************************************************************
 * count_node
 * Counts a node, and checks the limits. Returns true if the search
 * is to stop.
 ***************************************************************/
static int count_node(void)
{
    ++nodes;
    if (limits.nodes && nodes >= limits.nodes)
        stopped = 1;
    else if ((nodes & 1023) == 0 && limits.stop && limits.stop())
        stopped = 1;
    return stopped;
} // end of count_node

/***************************************************************
 * score_moves
 * Gives each move of a list its score for the order, in the same
 * place of order[].
 ***************************************************************/
static void score_moves(const move_t *list, int n, int tt_move, int ply)
{
    int32_t *sc = order + (list - move_stack);
    for (int i = 0; i < n; ++i)
    {
        move_t m = list[i];
        int captured = move_captured(m);
        int promoted = move_promoted(m);
        int v = 0;
        if (captured != EM)
            v = ORDER_CAPTURE + 8*iabs(captured) - iabs(move_piece(m));
        else if (m == killers[ply][0])
            v = ORDER_KILLER1;
        else if (m == killers[ply][1])
            v = ORDER_KILLER2;
        else
            v = history[move_piece(m) + 6][sq64[move_to(m)]];
        if (promoted == WQ || promoted == BQ)
            v += ORDER_QUEEN;
        sc[i] = v;
    }
    int t = tt_match(list, n, tt_move);
    if (t >= 0)
        sc[t] = ORDER_TT;
} // end of score_moves

/***************************************************************
 * pick_move
 * Brings the move of the highest score among list[i] to list[n-1]
 * to list[i], and returns it.
 ***************************************************************/
static move_t pick_move(move_t *list, int i, int n)
{
    int32_t *sc = order + (list - move_stack);
    int best = i;
    for (int j = i + 1; j < n; ++j)
        if (sc[j] > sc[best])
            best = j;
    move_t m = list[best];
    int32_t s = sc[best];
    list[best] = list[i];
    sc[best]   = sc[i];
    list[i] = m;
    sc[i]   = s;
    return m;
} // end of pick_move

/***************************************************************
 * shift_history
 * Divides each history by 1 << shift: by 2 with 1, and to 0 with
 * 31. One loop, for both, keeps the code small on NICE.
 ***************************************************************/
static void shift_history(int shift)
{
    int32_t *h = &history[0][0];
    for (int i = 0; i < 13*64; ++i)
        h[i] >>= shift;
} // end of shift_history

/***************************************************************
 * add_history
 * A quiet move m cut a node at depth: its history goes up by
 * depth * depth. When one reaches HISTORY_MAX, all are halved.
 ***************************************************************/
static void add_history(move_t m, int depth)
{
    int32_t *h = &history[move_piece(m) + 6][sq64[move_to(m)]];
    *h += depth * depth;
    if (*h >= HISTORY_MAX)
        shift_history(1);
} // end of add_history

/***************************************************************
 * update_pv
 * The line of ply is now m, and then the line of ply + 1.
 ***************************************************************/
static void update_pv(int ply, move_t m)
{
    int n = pv_length[ply + 1];
    if (n > ENGINE_MAX_PV - 1)
        n = ENGINE_MAX_PV - 1;
    pv_table[ply][0] = m;
    for (int i = 0; i < n; ++i)
        pv_table[ply][i + 1] = pv_table[ply + 1][i];
    pv_length[ply] = n + 1;
} // end of update_pv

/***************************************************************
 * is_draw
 * True if the position is a draw by the 50-move rule, or has come
 * up before, as far back as a repetition can reach.
 ***************************************************************/
static int is_draw(void)
{
    if (pos.halfmove >= 100)
        return 1;
    int back = pos.halfmove < pos.ply ? pos.halfmove : pos.ply;
    for (int i = 4; i <= back; i += 2)
        if (hist[pos.ply - i].hash == pos.hash)
            return 1;
    return 0;
} // end of is_draw

/***************************************************************
 * has_pieces
 * True if the side to move has a piece other than its king and
 * pawns. Without one, zugzwang is common, and the null move is
 * not tried.
 ***************************************************************/
static int has_pieces(void)
{
    const int s = pos.side;
    const int32_t *pieces = pos.pieces[s > 0];
    for (int i = 0; i < pos.count[s > 0]; ++i)
    {
        int p = for_side(pos.board[pieces[i]], s);
        if (p >= WN && p <= WQ)
            return 1;
    }
    return 0;
} // end of has_pieces

/***************************************************************
 * quiescence
 *
 * Searches only captures and promotions, until the position is
 * quiet. This avoids evaluating positions in the middle of an
 * exchange (the horizon effect). The side to move may always
 * decline to capture ("stand pat").
 ***************************************************************/
static int quiescence(int alpha, int beta, int ply)
{
    pv_length[ply] = 0;
    if (count_node())
        return 0;

    int best = evaluate();
    if (best >= beta || ply >= ENGINE_MAX_PLY - 1 || !move_stack_room())
        return best;
    if (best > alpha)
        alpha = best;

    move_t *list = move_stack + move_sp;
    int n = gen_captures(list);
    move_sp += n;
    score_moves(list, n, 0, ply);

    for (int i = 0; i < n; ++i)
    {
        move_t m = pick_move(list, i, n);
        make_move(m);
        if (!move_was_legal())
        {
            undo_move();
            continue;
        }
        int val = -quiescence(-beta, -alpha, ply + 1);
        undo_move();
        if (stopped)
            break;

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
 * Searches the position to depth ply, ply plies from the root, and
 * returns its value, from the point of view of the side to move:
 * -(ENGINE_MATE - ply) if it is mated, so that a nearer mate counts
 * more, and 0 if it is stalemated or a draw.
 ***************************************************************/
static int search(int alpha, int beta, int depth, int ply)
{
    pv_length[ply] = 0;
    int in_check = is_attacked(king_of(pos.side), pos.side);
    if (in_check)
        depth++;
    if (depth <= 0)
        return quiescence(alpha, beta, ply);

    if (count_node())
        return 0;
    if (is_draw())
        return 0;
    if (ply >= ENGINE_MAX_PLY - 1 || !move_stack_room())
        return evaluate();

    int tt_depth, tt_value, tt_bound, tt_move = 0;
    if (tt_probe(&tt_depth, &tt_value, &tt_bound, &tt_move, ply) && tt_depth >= depth)
    {
        if (tt_bound == TT_EXACT
                || (tt_bound == TT_LOWER && tt_value >= beta)
                || (tt_bound == TT_UPPER && tt_value <= alpha))
            return tt_value;
    }

    // Null-move pruning: if the side to move, after passing, still
    // scores at least beta in a shallower search, then a move would
    // too, and the node is cut. Not in check, not after a null move,
    // not in the principal variation (a full window), and not without
    // pieces, where zugzwang makes passing better than any move.
    if (!in_check && depth >= NULL_MIN_DEPTH && beta - alpha == 1
            && hist[pos.ply - 1].move != 0
            && evaluate() >= beta && has_pieces())
    {
        make_null_move();
#ifdef CHECK_STATE
        assert(board_consistent());
#endif
        int val = -search(-beta, -beta + 1, depth - 1 - NULL_R, ply + 1);
        undo_null_move();
        if (stopped)
            return 0;
        if (val >= beta)
            return val > ENGINE_MATE - ENGINE_MAX_PLY ? beta : val;
    }

    move_t *list = move_stack + move_sp;
    int n = gen_pseudo(list);
    move_sp += n;
    score_moves(list, n, tt_move, ply);

    int alpha0 = alpha;
    int best = -INF;
    move_t best_move = 0;
    int searched = 0;

    for (int i = 0; i < n; ++i)
    {
        move_t m = pick_move(list, i, n);
        make_move(m);
        if (!move_was_legal())
        {
            undo_move();
            continue;
        }
        // The first move with the full window, the rest with a null
        // window, and again with the full window if one is better. A
        // late quiet move that gives no check, out of check, is first
        // searched shallower, and again at the full depth if it beats
        // alpha.
        int val;
        if (searched++ == 0)
            val = -search(-beta, -alpha, depth - 1, ply + 1);
        else
        {
            int r = 0;
            if (depth >= LMR_MIN_DEPTH && searched > LMR_MIN_MOVES && !in_check
                    && move_captured(m) == EM && move_promoted(m) == EM
                    && m != killers[ply][0] && m != killers[ply][1]
                    && !is_attacked(king_of(pos.side), pos.side))
                r = LMR_R;
            val = -search(-alpha - 1, -alpha, depth - 1 - r, ply + 1);
            if (r && val > alpha && !stopped)
                val = -search(-alpha - 1, -alpha, depth - 1, ply + 1);
            if (val > alpha && val < beta && !stopped)
                val = -search(-beta, -alpha, depth - 1, ply + 1);
        }
        undo_move();
        if (stopped)
            break;

        if (val > best)
        {
            best = val;
            best_move = m;
            if (val > alpha)
            {
                alpha = val;
                update_pv(ply, m);
            }
        }
        if (alpha >= beta)
        {
            if (move_captured(m) == EM)
            {
                add_history(m, depth);
                if (m != killers[ply][0])
                {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
            }
            break;
        }
    }

    move_sp -= n;
    if (stopped)
        return 0;

    if (best_move == 0)
        return in_check ? -(ENGINE_MATE - ply) : 0;

    tt_store(depth, best,
             best <= alpha0 ? TT_UPPER : best >= beta ? TT_LOWER : TT_EXACT,
             best_move, ply);
    return best;
} // end of search

/***************************************************************
 * engine_search
 *
 * Iterative deepening: searches the root moves to depth 1, 2, 3,
 * and so on, each time with the best move of the last iteration
 * first. When a limit stops an iteration, the best move so far of
 * that iteration is kept if it was searched to the end, as it was
 * then better than the best move of the iteration before (which was
 * searched first).
 ***************************************************************/
move_t engine_search(const engine_limits_t *lim, engine_info_fn info)
{
    limits   = *lim;
    nodes    = 0;
    stopped  = 0;
    for (int i = 0; i < ENGINE_MAX_PLY; ++i)
        killers[i][0] = killers[i][1] = 0;
    shift_history(31);

    move_t *list = move_stack + move_sp;
    int n = gen_legal(list);
    if (n == 0)
        return 0;
    move_sp += n;

    int d, v, b, tt_move = 0;
    tt_probe(&d, &v, &b, &tt_move, 0);
    score_moves(list, n, tt_move, 0);
    for (int i = 0; i < n; ++i)
        pick_move(list, i, n);

    int max_depth = ENGINE_MAX_PLY - 1;
    if (limits.depth > 0 && limits.depth < max_depth)
        max_depth = limits.depth;

    move_t best_move = list[0];
    for (int depth = 1; depth <= max_depth; ++depth)
    {
        int alpha = -INF;
        int best = -INF;
        int best_index = -1;

        for (int i = 0; i < n; ++i)
        {
            make_move(list[i]);
            int val;
            if (i == 0)
                val = -search(-INF, -alpha, depth - 1, 1);
            else
            {
                val = -search(-alpha - 1, -alpha, depth - 1, 1);
                if (val > alpha && !stopped)
                    val = -search(-INF, -alpha, depth - 1, 1);
            }
            undo_move();
            if (stopped)
                break;
            if (val > best)
            {
                best = val;
                best_index = i;
                alpha = val;
                update_pv(0, list[i]);
            }
        }

        if (best_index < 0)
            break;

        // The best move to the front, and the rest in their order.
        best_move = list[best_index];
        for (int i = best_index; i > 0; --i)
            list[i] = list[i - 1];
        list[0] = best_move;

        if (stopped)
            break;

        tt_store(depth, best, TT_EXACT, best_move, 0);
        if (info)
        {
            engine_info_t in;
            in.depth = depth;
            in.score = best;
            in.nodes = nodes;
            in.pv_len = pv_length[0];
            for (int i = 0; i < pv_length[0]; ++i)
                in.pv[i] = pv_table[0][i];
            if (info(&in))
                break;
        }

        // Stop at one legal move, or at a mate that this depth sees in full.
        if (n == 1)
            break;
        if ((best > ENGINE_MATE - ENGINE_MAX_PLY || best < -ENGINE_MATE + ENGINE_MAX_PLY)
                && ENGINE_MATE - iabs(best) <= depth)
            break;
    }

    move_sp -= n;
    return best_move;
} // end of engine_search
