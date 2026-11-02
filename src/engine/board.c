/***************************************************************
 * board.c: the position, its Zobrist keys, and attacks.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

position_t pos;
history_t  hist[MAX_HISTORY];

// Centipawns, by piece + 6: black's are negative.
const int32_t piece_value[13] = {
    0, -900, -500, -300, -300, -100, 0, 100, 300, 300, 500, 900, 0
};

uint32_t zob_piece[13][64];
uint32_t zob_castle[16];
uint32_t zob_ep[8];
uint32_t zob_side;
int32_t  sq64[120];

/***************************************************************
 * board_init
 * Fills in the tables: the Zobrist keys come from a xorshift
 * generator with a fixed seed, so they are the same on every run.
 ***************************************************************/
void board_init(void)
{
    static int done = 0;
    if (done)
        return;
    done = 1;

    uint32_t x = 2463534242u;
#define NEXT() (x ^= x << 13, x ^= x >> 17, x ^= x << 5, x)

    for (int p = 0; p < 13; ++p)
        for (int s = 0; s < 64; ++s)
            zob_piece[p][s] = (p == EM + 6) ? 0 : NEXT();

    // The key of a set of rights is the xor of the keys of its bits.
    uint32_t bits[4];
    for (int i = 0; i < 4; ++i)
        bits[i] = NEXT();
    for (int c = 0; c < 16; ++c)
    {
        zob_castle[c] = 0;
        for (int i = 0; i < 4; ++i)
            if (c & (1 << i))
                zob_castle[c] ^= bits[i];
    }

    for (int f = 0; f < 8; ++f)
        zob_ep[f] = NEXT();
    zob_side = NEXT();
#undef NEXT

    for (int i = 0; i < 120; ++i)
        sq64[i] = 0;
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
            sq64[square(f, r)] = r * 8 + f;
} // end of board_init

/***************************************************************
 * board_clear
 * An empty board, with white to move and no history.
 ***************************************************************/
void board_clear(void)
{
    board_init();
    for (int i = 0; i < 120; ++i)
        pos.board[i] = IV;
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f)
            pos.board[square(f, r)] = EM;
    pos.side     = 1;
    pos.castle   = 0;
    pos.ep       = 0;
    pos.halfmove = 0;
    pos.fullmove = 1;
    pos.material = 0;
    pos.king[0]  = 0;
    pos.king[1]  = 0;
    pos.ply      = 0;
    pos.hash     = 0;
} // end of board_clear

/***************************************************************
 * board_put
 * Puts a piece on an empty square, while setting up a position.
 * The hash is computed afterwards, by board_compute_hash.
 ***************************************************************/
void board_put(int sq, int piece)
{
    pos.board[sq] = piece;
    pos.material += piece_value[piece + 6];
    if (piece == WK) pos.king[1] = sq;
    if (piece == BK) pos.king[0] = sq;
} // end of board_put

/***************************************************************
 * board_compute_hash
 ***************************************************************/
uint32_t board_compute_hash(void)
{
    uint32_t h = 0;
    for (int sq = A1; sq <= H8; ++sq)
    {
        int p = pos.board[sq];
        if (p != IV && p != EM)
            h ^= zob_piece[p + 6][sq64[sq]];
    }
    h ^= zob_castle[pos.castle];
    if (pos.ep)
        h ^= zob_ep[file_of(pos.ep)];
    if (pos.side < 0)
        h ^= zob_side;
    return h;
} // end of board_compute_hash

/***************************************************************
 * is_attacked
 * Returns true if the side opposite to "side" attacks the square.
 ***************************************************************/
int is_attacked(int sq, int side)
{
    static const int32_t knight_dirs[8] = {21, 19, 12, 8, -8, -12, -19, -21};
    static const int32_t diag_dirs[4]   = {NE, NW, SE, SW};
    static const int32_t line_dirs[4]   = {N, S, E, W};
    const int32_t *bd = pos.board;
    const int e = -side; // The sign of the attacking pieces

    // Pawns: an enemy pawn attacks sq from one rank ahead of it
    if (bd[sq + 10*side + 1] == e*WP || bd[sq + 10*side - 1] == e*WP)
        return 1;

    for (int k = 0; k < 8; ++k)
        if (bd[sq + knight_dirs[k]] == e*WN)
            return 1;

    for (int k = 0; k < 4; ++k)
    {
        int d = diag_dirs[k];
        int p = bd[sq + d];
        if (p == e*WK)
            return 1;
        int s2 = sq + d;
        while (p == EM)
        {
            s2 += d;
            p = bd[s2];
        }
        if (p == e*WB || p == e*WQ)
            return 1;
    }

    for (int k = 0; k < 4; ++k)
    {
        int d = line_dirs[k];
        int p = bd[sq + d];
        if (p == e*WK)
            return 1;
        int s2 = sq + d;
        while (p == EM)
        {
            s2 += d;
            p = bd[s2];
        }
        if (p == e*WR || p == e*WQ)
            return 1;
    }

    return 0;
} // end of is_attacked
