/***************************************************************
 * makemove.c: making and taking back moves.
 *
 * make_move keeps the evaluation's sums, the king squares, and
 * the Zobrist key up to date as it goes. undo_move restores the state and the
 * key from the history, and reverses the rest.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

// The castling rights that remain after a move from or to a square.
static int32_t castle_keep(int sq)
{
    switch (sq)
    {
        case A1 : return ~CASTLE_WQ;
        case E1 : return ~(CASTLE_WK | CASTLE_WQ);
        case H1 : return ~CASTLE_WK;
        case A8 : return ~CASTLE_BQ;
        case E8 : return ~(CASTLE_BK | CASTLE_BQ);
        case H8 : return ~CASTLE_BK;
        default : return ~0;
    }
}

static inline uint32_t zob(int piece, int sq)
{
    return zob_piece[piece + 6][sq64[sq]];
}

static inline int32_t pv(int piece, int sq)
{
    return psqt[piece + 6][sq64[sq]];
}

// The change that a move makes to pos.psqt, which is the same as
// make_move makes it and as undo_move takes it back. side is the
// side that moves.
static int32_t psqt_delta(move_t m, int side)
{
    int from     = move_from(m);
    int to       = move_to(m);
    int piece    = move_piece(m);
    int captured = move_captured(m);
    int promoted = move_promoted(m);
    int kind     = move_kind(m);

    int32_t d = pv((promoted != EM) ? promoted : piece, to) - pv(piece, from);
    if (captured != EM)
        d -= pv(captured, (kind == MK_EP) ? to - N*side : to);
    if (kind == MK_CASTLE)
    {
        int rook = side*WR;
        d += (to > from) ? pv(rook, to - 1) - pv(rook, to + 1)
                         : pv(rook, to + 1) - pv(rook, to - 2);
    }
    return d;
}

// The change that a move makes to pos.phase.
static int phase_delta(move_t m)
{
    int d = -phase_value[move_captured(m) + 6];
    int promoted = move_promoted(m);
    if (promoted != EM)
        d += phase_value[promoted + 6];
    return d;
}

/***************************************************************
 * make_move
 * Makes a pseudo-legal move. The caller checks move_was_legal.
 ***************************************************************/
void make_move(move_t m)
{
    int from     = move_from(m);
    int to       = move_to(m);
    int piece    = move_piece(m);
    int captured = move_captured(m);
    int promoted = move_promoted(m);
    int kind     = move_kind(m);
    uint32_t h   = pos.hash;

    assert(pos.ply < MAX_HISTORY);
    history_t *hs = &hist[pos.ply++];
    hs->move  = m;
    hs->state = pos.castle | pos.ep << 4 | pos.halfmove << 11;
    hs->hash  = h;

    int us = pos.side > 0;
    if (captured != EM)
    {
        int csq = (kind == MK_EP) ? to + for_side(S, pos.side) : to;
        int slot = pos.index[csq];
        pos.pieces[!us][slot] = 0;
        hs->slot = slot;
        pos.board[csq] = EM;
        h ^= zob(captured, csq);
    }

    int arrives = (promoted != EM) ? promoted : piece;
    pos.board[from] = EM;
    pos.board[to]   = arrives;
    pos.index[to]   = pos.index[from];
    pos.pieces[us][pos.index[to]] = to;
    h ^= zob(piece, from) ^ zob(arrives, to);

    if (kind == MK_CASTLE)
    {
        int rook = pos.side*WR;
        int rfrom = (to > from) ? to + 1 : to - 2;
        int rto   = (to > from) ? to - 1 : to + 1;
        pos.board[rfrom] = EM;
        pos.board[rto]   = rook;
        pos.index[rto]   = pos.index[rfrom];
        pos.pieces[us][pos.index[rto]] = rto;
        h ^= zob(rook, rfrom) ^ zob(rook, rto);
    }

    if (piece == WK) pos.king[1] = to;
    if (piece == BK) pos.king[0] = to;

    pos.psqt  += psqt_delta(m, pos.side);
    pos.phase += phase_delta(m);

    h ^= zob_castle[pos.castle];
    pos.castle &= castle_keep(from) & castle_keep(to);
    h ^= zob_castle[pos.castle];

    if (pos.ep)
        h ^= zob_ep[file_of(pos.ep)];
    pos.ep = 0;
    if (kind == MK_DOUBLE)
    {
        pos.ep = (from + to) / 2;
        h ^= zob_ep[file_of(pos.ep)];
    }

    if (captured != EM || piece == WP || piece == BP)
        pos.halfmove = 0;
    else
        pos.halfmove++;

    if (pos.side < 0)
        pos.fullmove++;
    pos.side = -pos.side;
    h ^= zob_side;
    pos.hash = h;
} // end of make_move

/***************************************************************
 * undo_move
 * Takes back the last move made.
 ***************************************************************/
void undo_move(void)
{
    assert(pos.ply > 0);
    history_t *hs = &hist[--pos.ply];
    move_t m = hs->move;
    int from     = move_from(m);
    int to       = move_to(m);
    int piece    = move_piece(m);
    int captured = move_captured(m);
    int kind     = move_kind(m);

    pos.side = -pos.side;
    if (pos.side < 0)
        pos.fullmove--;

    int us = pos.side > 0;
    pos.board[from] = piece;
    pos.board[to]   = EM;
    pos.index[from] = pos.index[to];
    pos.pieces[us][pos.index[from]] = from;
    if (captured != EM)
    {
        int csq = (kind == MK_EP) ? to + for_side(S, pos.side) : to;
        int slot = hs->slot;
        pos.pieces[!us][slot] = csq;
        pos.index[csq] = slot;
        pos.board[csq] = captured;
    }

    if (kind == MK_CASTLE)
    {
        int rfrom = (to > from) ? to + 1 : to - 2;
        int rto   = (to > from) ? to - 1 : to + 1;
        pos.board[rto]   = EM;
        pos.board[rfrom] = pos.side*WR;
        pos.index[rfrom] = pos.index[rto];
        pos.pieces[us][pos.index[rfrom]] = rfrom;
    }

    if (piece == WK) pos.king[1] = from;
    if (piece == BK) pos.king[0] = from;

    pos.psqt    -= psqt_delta(m, pos.side);
    pos.phase   -= phase_delta(m);
    pos.castle   = hs->state & 0xF;
    pos.ep       = (hs->state >> 4) & 0x7F;
    pos.halfmove = hs->state >> 11;
    pos.hash     = hs->hash;
} // end of undo_move

/***************************************************************
 * make_null_move
 * Passes the move to the other side, for the search's null move.
 * It is in the history as move 0. The 50-move count starts again,
 * so that no repetition is looked for across it.
 ***************************************************************/
void make_null_move(void)
{
    assert(pos.ply < MAX_HISTORY);
    history_t *hs = &hist[pos.ply++];
    hs->move  = 0;
    hs->state = pos.castle | pos.ep << 4 | pos.halfmove << 11;
    hs->hash  = pos.hash;

    if (pos.ep)
        pos.hash ^= zob_ep[file_of(pos.ep)];
    pos.ep = 0;
    pos.halfmove = 0;
    pos.side = -pos.side;
    pos.hash ^= zob_side;
} // end of make_null_move

/***************************************************************
 * undo_null_move
 * Takes back make_null_move.
 ***************************************************************/
void undo_null_move(void)
{
    assert(pos.ply > 0 && hist[pos.ply - 1].move == 0);
    history_t *hs = &hist[--pos.ply];
    pos.side     = -pos.side;
    pos.ep       = (hs->state >> 4) & 0x7F;
    pos.halfmove = hs->state >> 11;
    pos.hash     = hs->hash;
} // end of undo_null_move

/***************************************************************
 * move_was_legal
 * After make_move: true if the king of the side that moved is not
 * attacked.
 ***************************************************************/
int move_was_legal(void)
{
    return !is_attacked(king_of(-pos.side), -pos.side);
} // end of move_was_legal
