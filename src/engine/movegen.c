/***************************************************************
 * movegen.c: move generation.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

move_t  move_stack[MOVE_STACK_SIZE];
int32_t move_sp;

#define INLINE static inline __attribute__((always_inline))

// A move of the piece on square i, with its to square and the piece
// captured still to add: TO(to - i), and CAP(captured).
#define BASE(i, piece) ((i) + ((i) << 7) + (((piece) + 6) << 14) + (6 << 18) + (6 << 22))

// The fields of a move that are added to BASE. Both may be negative,
// so they are multiplications, not shifts, which C leaves undefined
// for a negative number; the compiler makes the same shifts of them.
#define TO(d)  ((d) * (1 << 7))
#define CAP(t) ((t) * (1 << 18))

// True if t is a piece of the side that does not move. s is constant
// where this is inlined. The squares hold -6 to 6 and IV, 99, so as
// unsigned numbers black's pieces are the ones above IV, and white's
// are 1 to 6. Unsigned comparisons are one instruction shorter on NICE
// than signed ones.
INLINE int enemy(int t, const int s)
{
    return (s > 0) ? ((uint32_t) t > (uint32_t) IV) : ((uint32_t) (t - 1) < 6u);
}

// The same, for a square that is known not to be empty.
INLINE int enemy_piece(int t, const int s)
{
    return (s > 0) ? ((uint32_t) t > (uint32_t) IV) : ((uint32_t) t < 7u);
}

/***************************************************************
 * add_capture
 * Adds the capture of t on the square q by the piece on the
 * square from. Not inlined: the captures-only rays then keep
 * nothing of the move in their loops.
 ***************************************************************/
static __attribute__((noinline)) move_t *add_capture(move_t *out, const int32_t *from, const int32_t *q, int t)
{
    int i = (int) (from - pos.board);
    *out++ = BASE(i, *from) + TO((int) (q - from)) + CAP(t);
    return out;
}

/***************************************************************
 * add_pawn_moves
 * Adds a pawn move, as four moves if it promotes.
 ***************************************************************/
static move_t *add_pawn_moves(move_t *out, int s, int from, int to, int captured, int promotes)
{
    int piece = s*WP;
    if (promotes)
    {
        *out++ = move_new(from, to, piece, captured, s*WQ, MK_NORMAL);
        *out++ = move_new(from, to, piece, captured, s*WR, MK_NORMAL);
        *out++ = move_new(from, to, piece, captured, s*WB, MK_NORMAL);
        *out++ = move_new(from, to, piece, captured, s*WN, MK_NORMAL);
    }
    else
        *out++ = move_new(from, to, piece, captured, EM, MK_NORMAL);
    return out;
}

/***************************************************************
 * pawn_slow
 * The moves of a pawn that promotes, or that may capture en
 * passant: all the cases, and not fast.
 ***************************************************************/
static __attribute__((noinline)) move_t *pawn_slow(move_t *out, int i, int quiet)
{
    const int32_t *bd = pos.board;
    const int s = pos.side;
    int rank = rank_of(i);
    int promotes = (s > 0) ? (rank == 6) : (rank == 1);
    int start    = (s > 0) ? (rank == 1) : (rank == 6);
    int j = i + N*s;

    if (bd[j] == EM && (quiet || promotes))
    {
        out = add_pawn_moves(out, s, i, j, EM, promotes);
        if (start && quiet && bd[j + N*s] == EM)
            *out++ = move_new(i, j + N*s, s*WP, EM, EM, MK_DOUBLE);
    }

    for (int d = -1; d <= 1; d += 2)
    {
        j = i + N*s + d;
        if (bd[j] != IV && for_side(bd[j], s) < 0)
            out = add_pawn_moves(out, s, i, j, bd[j], promotes);
        else if (j == pos.ep)
            *out++ = move_new(i, j, s*WP, -s*WP, EM, MK_EP);
    }
    return out;
}

// One step in direction d, for a knight or the king.
INLINE move_t *leap(move_t *out, const int32_t *from, int i, const int piece, const int d, const int s, const int quiet)
{
    int t = from[d];
    if (quiet)
    {
        if (t == EM)
            *out++ = BASE(i, piece) + TO(d);
        else if (enemy_piece(t, s))
            *out++ = BASE(i, piece) + TO(d) + CAP(t);
    }
    else if (enemy(t, s))
        *out++ = BASE(i, piece) + TO(d) + CAP(t);
    return out;
}

// The moves in direction d up to the first piece, for a slider.
INLINE move_t *ray(move_t *out, const int32_t *from, int i, int piece, const int d, const int s, const int quiet)
{
    const int32_t *q = from + d;
    int t;
    if (quiet)
    {
        move_t m = BASE(i, piece) + TO(d);
        while ((t = *q) == EM)
        {
            *out++ = m;
            m += TO(d);
            q += d;
        }
        if (enemy_piece(t, s))
            *out++ = m + CAP(t);
    }
    else
    {
        // The first square by itself: many rays end there.
        if ((t = *q) == EM)
        {
            do
                q += d;
            while ((t = *q) == EM);
        }
        if (enemy_piece(t, s))
            out = add_capture(out, from, q, t);
    }
    return out;
}

/***************************************************************
 * gen
 * The moves of side s but for castling. s and quiet are constants
 * where this is inlined, so each of the four cases gets its own
 * code, without a test of either.
 ***************************************************************/
INLINE int gen(move_t *list, const int s, const int quiet)
{
    const int32_t *bd = pos.board;
    const int32_t *pieces = pos.pieces[s > 0];
    const int32_t *end = pieces + pos.count[s > 0];
    const int fwd = N*s;
    // A pawn on its seventh rank promotes. When there is an en
    // passant square, a pawn on its fifth rank may capture there.
    // Both take pawn_slow: white's pawns from a5 or a7 on, black's up
    // to h4 or h2.
    const int thr = (s > 0) ? (pos.ep ? square(0, 4) : square(0, 6))
                            : (pos.ep ? square(7, 3) : square(7, 1));
    move_t *out = list;

    assert(pieces != end); // The king is in the list

    do
    {
        const int i = *pieces++;
        const int32_t *from = bd + i;
        const int piece = *from;

        switch ((s > 0) ? piece : -piece) // WP to WK, or +-IV if captured (square 0)
        {
            case WP :
            {
                int t;
                if ((s > 0) ? ((uint32_t) i >= (uint32_t) thr) : ((uint32_t) i <= (uint32_t) thr))
                {
                    out = pawn_slow(out, i, quiet);
                    continue;
                }
                if (quiet && from[fwd] == EM)
                {
                    *out++ = BASE(i, s*WP) + TO(fwd);
                    if (((s > 0) ? (i <= square(7, 1)) : (i >= square(0, 6))) && from[2*fwd] == EM) // On its second rank
                        *out++ = BASE(i, s*WP) + TO(2*fwd) + (MK_DOUBLE << 26);
                }
                t = from[fwd - 1];
                if (enemy(t, s))
                    *out++ = BASE(i, s*WP) + TO(fwd - 1) + CAP(t);
                t = from[fwd + 1];
                if (enemy(t, s))
                    *out++ = BASE(i, s*WP) + TO(fwd + 1) + CAP(t);
                continue;
            }

            case WN :
                out = leap(out, from, i, s*WN, 21, s, quiet);
                out = leap(out, from, i, s*WN, 19, s, quiet);
                out = leap(out, from, i, s*WN, 12, s, quiet);
                out = leap(out, from, i, s*WN, 8, s, quiet);
                out = leap(out, from, i, s*WN, -8, s, quiet);
                out = leap(out, from, i, s*WN, -12, s, quiet);
                out = leap(out, from, i, s*WN, -19, s, quiet);
                out = leap(out, from, i, s*WN, -21, s, quiet);
                continue;

            case WB :
            case WQ :
                out = ray(out, from, i, piece, NE, s, quiet);
                out = ray(out, from, i, piece, NW, s, quiet);
                out = ray(out, from, i, piece, SE, s, quiet);
                out = ray(out, from, i, piece, SW, s, quiet);
                if (piece == s*WB)
                    continue;
                __attribute__((fallthrough)); // The queen
            case WR :
                out = ray(out, from, i, piece, N, s, quiet);
                out = ray(out, from, i, piece, E, s, quiet);
                out = ray(out, from, i, piece, S, s, quiet);
                out = ray(out, from, i, piece, W, s, quiet);
                continue;

            case WK :
                out = leap(out, from, i, s*WK, NE, s, quiet);
                out = leap(out, from, i, s*WK, NW, s, quiet);
                out = leap(out, from, i, s*WK, SE, s, quiet);
                out = leap(out, from, i, s*WK, SW, s, quiet);
                out = leap(out, from, i, s*WK, N, s, quiet);
                out = leap(out, from, i, s*WK, E, s, quiet);
                out = leap(out, from, i, s*WK, S, s, quiet);
                out = leap(out, from, i, s*WK, W, s, quiet);
                continue;

            default :
                continue;
        }
    } while (pieces != end);

    return (int) (out - list);
}

/***************************************************************
 * gen_moves
 * Writes pseudo-legal moves to list, and returns their number.
 * A pseudo-legal move may leave the mover's own king in check.
 * Castling is generated only when the king is not in check and
 * does not pass an attacked square.
 *
 * If quiet is 0, only the captures and the promotions, for the
 * quiescence search.
 ***************************************************************/
int gen_moves(move_t *list, int quiet)
{
    const int32_t *bd = pos.board;
    const int s = pos.side;
    int n;

    if (s > 0)
        n = quiet ? gen(list, 1, 1) : gen(list, 1, 0);
    else
        n = quiet ? gen(list, -1, 1) : gen(list, -1, 0);

    // Castling. The king must be on its square and the rook on its
    // corner; the rights alone are not trusted, since a FEN may be wrong.
    if (quiet)
    {
        int base = (s > 0) ? 0 : 70; // Rank 1 or rank 8
        int king = s*WK, rook = s*WR;
        int rights = pos.castle >> ((s > 0) ? 0 : 2);

        if ((rights & 3) && bd[E1+base] == king && !is_attacked(E1+base, s))
        {
            if ((rights & 1) && bd[H1+base] == rook
                    && bd[F1+base] == EM && bd[G1+base] == EM
                    && !is_attacked(F1+base, s))
                list[n++] = move_new(E1+base, G1+base, king, EM, EM, MK_CASTLE);

            if ((rights & 2) && bd[A1+base] == rook
                    && bd[D1+base] == EM && bd[C1+base] == EM && bd[B1+base] == EM
                    && !is_attacked(D1+base, s))
                list[n++] = move_new(E1+base, C1+base, king, EM, EM, MK_CASTLE);
        }
    }

    assert(n <= ENGINE_MAX_MOVES);
    return n;
} // end of gen_moves

/***************************************************************
 * gen_legal
 * Writes the legal moves to list, and returns their number.
 ***************************************************************/
int gen_legal(move_t *list)
{
    int n = gen_pseudo(list);
    int k = 0;
    for (int i = 0; i < n; ++i)
    {
        make_move(list[i]);
        int ok = move_was_legal();
        undo_move();
        if (ok)
            list[k++] = list[i];
    }
    return k;
} // end of gen_legal
