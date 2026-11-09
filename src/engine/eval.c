/***************************************************************
 * eval.c: the evaluation of a position.
 *
 * The value of a position is the sum, over its pieces, of a value
 * that depends on the piece and its square: its material, plus a
 * piece-square bonus. make_move and undo_move keep the sum up to
 * date, so evaluate only reads it.
 *
 * There are two sums, one for the middle game and one for the end
 * game, which differ for the king (to the corner in the middle game,
 * to the center in the end game) and for pawns (worth more as they
 * advance in the end game). evaluate blends the two by the phase:
 * the material left on the board, counting a knight or bishop 1, a
 * rook 2, and a queen 4, which is 24 at the start.
 *
 * The two sums are packed in one word, the end game in the upper
 * half and the middle game in the lower half, so that one addition
 * updates both. Each stays well within 16 bits.
 *
 * The tables are those of the "Simplified Evaluation Function" of
 * Tomasz Michniewski (chessprogramming.org), but for the end-game
 * pawns.
 *
 * Below full strength (engine_set_strength), evaluate adds noise of
 * up to plus or minus an amplitude, from a hash of the Zobrist key and
 * a seed. A position thus gets the same value each time the search
 * meets it, so the transposition table stays consistent, and the
 * seed varies it from game to game. The noise is not in the
 * incremental sums.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

int32_t psqt[13][64];

const int32_t phase_value[13] = {
    0, 4, 2, 1, 1, 0, 0, 0, 1, 1, 2, 4, 0
};

#define PACK(mg, eg) ((int32_t) ((uint32_t) (eg) << 16) + (mg))

static inline int unpack_mg(int32_t s) { return (int16_t) s; }
static inline int unpack_eg(int32_t s) { return (s + 0x8000) >> 16; }

// Material, by piece: pawn, knight, bishop, rook, queen, king.
static const int16_t value_mg[6] = {100, 320, 330, 500, 900, 0};
static const int16_t value_eg[6] = {120, 320, 330, 500, 900, 0};

// The bonus of a white piece on each square, as seen from white:
// a8 is the first entry, and h1 the last.
static const int8_t pst[7][64] = {
    { // Pawn
          0,   0,   0,   0,   0,   0,   0,   0,
         50,  50,  50,  50,  50,  50,  50,  50,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    { // Knight
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    },
    { // Bishop
        -20, -10, -10, -10, -10, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   5,   5,  10,  10,   5,   5, -10,
        -10,   0,  10,  10,  10,  10,   0, -10,
        -10,  10,  10,  10,  10,  10,  10, -10,
        -10,   5,   0,   0,   0,   0,   5, -10,
        -20, -10, -10, -10, -10, -10, -10, -20
    },
    { // Rook
          0,   0,   0,   0,   0,   0,   0,   0,
          5,  10,  10,  10,  10,  10,  10,   5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
         -5,   0,   0,   0,   0,   0,   0,  -5,
          0,   0,   0,   5,   5,   0,   0,   0
    },
    { // Queen
        -20, -10, -10,  -5,  -5, -10, -10, -20,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
         -5,   0,   5,   5,   5,   5,   0,  -5,
          0,   0,   5,   5,   5,   5,   0,  -5,
        -10,   5,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,   0,   0,   0,   0, -10,
        -20, -10, -10,  -5,  -5, -10, -10, -20
    },
    { // King, in the middle game
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20
    },
    { // King, in the end game
        -50, -40, -30, -20, -20, -30, -40, -50,
        -30, -20, -10,   0,   0, -10, -20, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -30,   0,   0,   0,   0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50
    }
};

// End-game pawns: a bonus by rank, 0 to 7, for a passed pawn to run.
static const int8_t pawn_eg[8] = {0, 0, 5, 15, 30, 50, 80, 0};

/***************************************************************
 * eval_init
 * Fills in psqt, signed: black's values are negative, and black's
 * tables are white's, mirrored from rank to rank.
 ***************************************************************/
void eval_init(void)
{
    for (int p = 1; p <= 6; ++p)
    {
        for (int s = 0; s < 64; ++s)
        {
            int i = s ^ 56;  // The entry of a white piece on square s
            int mg = value_mg[p - 1] + pst[p == 6 ? 5 : p - 1][i];
            int eg = value_eg[p - 1];
            if (p == 1)
                eg += pawn_eg[s >> 3];
            else
                eg += pst[p == 6 ? 6 : p - 1][i];
            psqt[ p + 6][s]      =  PACK(mg, eg);
            psqt[-p + 6][s ^ 56] = -PACK(mg, eg);
        }
    }
} // end of eval_init

/***************************************************************
 * evaluate
 * Returns the value of the position in centipawns, from the point
 * of view of the side to move.
 ***************************************************************/
static int      noise_amplitude;
static uint32_t noise_seed;

void eval_set_noise(int amplitude, uint32_t seed)
{
    noise_amplitude = amplitude;
    noise_seed = seed;
} // end of eval_set_noise

// From -noise_amplitude to noise_amplitude, by a hash of the Zobrist
// key and the seed (the finalizer of MurmurHash3).
static int noise(void)
{
    uint32_t x = pos.hash ^ noise_seed;
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return (int) (x % (uint32_t) (2 * noise_amplitude + 1)) - noise_amplitude;
} // end of noise

int evaluate(void)
{
    int phase = pos.phase < 24 ? pos.phase : 24;
    int mg = unpack_mg(pos.psqt);
    int eg = unpack_eg(pos.psqt);
    int v = (mg * phase + eg * (24 - phase)) / 24;
    if (pos.side < 0)
        v = -v;
    return noise_amplitude ? v + noise() : v;
} // end of evaluate
