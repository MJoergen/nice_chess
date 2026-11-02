/***************************************************************
 * movegen.c: move generation.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

move_t  move_stack[MOVE_STACK_SIZE];
int32_t move_sp;

/***************************************************************
 * add_pawn_moves
 * Adds a pawn move, as four moves if it promotes.
 ***************************************************************/
static int add_pawn_moves(move_t *list, int n, int s, int from, int to, int captured, int promotes)
{
    int piece = s*WP;
    if (promotes)
    {
        list[n++] = move_new(from, to, piece, captured, s*WQ, MK_NORMAL);
        list[n++] = move_new(from, to, piece, captured, s*WR, MK_NORMAL);
        list[n++] = move_new(from, to, piece, captured, s*WB, MK_NORMAL);
        list[n++] = move_new(from, to, piece, captured, s*WN, MK_NORMAL);
    }
    else
        list[n++] = move_new(from, to, piece, captured, EM, MK_NORMAL);
    return n;
}

/***************************************************************
 * gen_pseudo
 * Writes all pseudo-legal moves to list, and returns their number.
 * A pseudo-legal move may leave the mover's own king in check.
 * Castling is generated only when the king is not in check and
 * does not pass an attacked square.
 ***************************************************************/
int gen_pseudo(move_t *list)
{
    static const int32_t dirs[16] = {
        NE, NW, SE, SW, N, E, S, W,         // Bishop 0-3, rook 4-7, queen and king 0-7
        21, 19, 12, 8, -8, -12, -19, -21    // Knight 8-15
    };

    const int32_t *bd = pos.board;
    const int s = pos.side;
    int n = 0;

    for (int i = A1; i <= H8; ++i)
    {
        int piece = bd[i];
        int first, last, slide;

        if (piece * s <= 0 || piece == IV) // Off the board, empty, or the opponent's
            continue;

        switch (piece * s)
        {
            case WP :
            {
                int rank = rank_of(i);
                int promotes = (s > 0) ? (rank == 6) : (rank == 1);
                int start    = (s > 0) ? (rank == 1) : (rank == 6);
                int j = i + N*s;

                if (bd[j] == EM)
                {
                    n = add_pawn_moves(list, n, s, i, j, EM, promotes);
                    if (start && bd[j + N*s] == EM)
                        list[n++] = move_new(i, j + N*s, piece, EM, EM, MK_DOUBLE);
                }

                for (int d = -1; d <= 1; d += 2)
                {
                    j = i + N*s + d;
                    if (bd[j] != IV && bd[j]*s < 0)
                        n = add_pawn_moves(list, n, s, i, j, bd[j], promotes);
                    else if (j == pos.ep)
                        list[n++] = move_new(i, j, piece, -s*WP, EM, MK_EP);
                }
                continue;
            }

            case WN : first = 8; last = 16; slide = 0; break;
            case WB : first = 0; last = 4;  slide = 1; break;
            case WR : first = 4; last = 8;  slide = 1; break;
            case WQ : first = 0; last = 8;  slide = 1; break;
            case WK : first = 0; last = 8;  slide = 0; break;
            default : continue;
        }

        for (int k = first; k < last; ++k)
        {
            int d = dirs[k];
            int j = i;
            while (1)
            {
                j += d;
                int target = bd[j];
                if (target == IV || target*s > 0) // Off the board, or our own piece
                    break;
                list[n++] = move_new(i, j, piece, target, EM, MK_NORMAL);
                if (target != EM || !slide)
                    break;
            }
        }
    }

    // Castling. The king must be on its square and the rook on its
    // corner; the rights alone are not trusted, since a FEN may be wrong.
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
} // end of gen_pseudo

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
