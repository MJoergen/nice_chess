/***************************************************************
 * eval.c: the evaluation of a position.
 *
 * The value of a position is the sum, over its pieces, of a value
 * that depends on the piece and its square: its material, plus a
 * piece-square bonus. make_move and undo_move keep the sum up to
 * date, so evaluate only reads it.
 *
 * There are two sums, one for the middle game and one for the end
 * game, each with its own material and tables. evaluate blends the
 * two by the phase: the material left on the board, counting a
 * knight or bishop 1, a rook 2, and a queen 4, which is 24 at the
 * start.
 *
 * The two sums are packed in one word, the end game in the upper
 * half and the middle game in the lower half, so that one addition
 * updates both. Each stays well within 16 bits.
 *
 * The material and the tables are those of PeSTO, by Ronald
 * Friederich, from Rofchade, tuned on game results
 * (chessprogramming.org, "PeSTO's Evaluation Function").
 *
 * When one side has only its king, and the other pieces of a phase
 * of 2 to 4, from a rook or two minor pieces to a queen or two
 * rooks, with pawns or without, a mop-up term (chessprogramming.org,
 * "Mop-up Evaluation") helps the stronger side to mate: a bonus by
 * the bare king's distance from the centre, and by how near the
 * stronger king is to it. With a bishop and a knight, the mate is
 * only in a corner of the bishop's colour, so the bonus is by the
 * bare king's nearness to such a corner instead, and the kings'
 * nearness counts more; that is so with pawns beside them too, where
 * it mates more often than the other bonus. The tables pull both
 * kings to the centre alike, and a mate can be deeper than the
 * search, so without the term the engine may not make progress. With
 * more pieces than a phase of 4 the search mates without it, and the
 * test of the phase keeps it off the path of the other positions
 * (LOG.md, 2026-11-13).
 *
 * Below full strength (engine_set_strength), and at the levels of the
 * windowed front end (engine_set_noise), evaluate adds noise of
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
static const int16_t value_mg[6] = {82, 337, 365, 477, 1025, 0};
static const int16_t value_eg[6] = {94, 281, 297, 512, 936, 0};

// The bonus of a white piece on each square, as seen from white:
// a8 is the first entry, and h1 the last.
static const int16_t pst_mg[6][64] = {
    { // Pawn
           0,    0,    0,    0,    0,    0,    0,    0,
          98,  134,   61,   95,   68,  126,   34,  -11,
          -6,    7,   26,   31,   65,   56,   25,  -20,
         -14,   13,    6,   21,   23,   12,   17,  -23,
         -27,   -2,   -5,   12,   17,    6,   10,  -25,
         -26,   -4,   -4,  -10,    3,    3,   33,  -12,
         -35,   -1,  -20,  -23,  -15,   24,   38,  -22,
           0,    0,    0,    0,    0,    0,    0,    0
    },
    { // Knight
        -167,  -89,  -34,  -49,   61,  -97,  -15, -107,
         -73,  -41,   72,   36,   23,   62,    7,  -17,
         -47,   60,   37,   65,   84,  129,   73,   44,
          -9,   17,   19,   53,   37,   69,   18,   22,
         -13,    4,   16,   13,   28,   19,   21,   -8,
         -23,   -9,   12,   10,   19,   17,   25,  -16,
         -29,  -53,  -12,   -3,   -1,   18,  -14,  -19,
        -105,  -21,  -58,  -33,  -17,  -28,  -19,  -23
    },
    { // Bishop
         -29,    4,  -82,  -37,  -25,  -42,    7,   -8,
         -26,   16,  -18,  -13,   30,   59,   18,  -47,
         -16,   37,   43,   40,   35,   50,   37,   -2,
          -4,    5,   19,   50,   37,   37,    7,   -2,
          -6,   13,   13,   26,   34,   12,   10,    4,
           0,   15,   15,   15,   14,   27,   18,   10,
           4,   15,   16,    0,    7,   21,   33,    1,
         -33,   -3,  -14,  -21,  -13,  -12,  -39,  -21
    },
    { // Rook
          32,   42,   32,   51,   63,    9,   31,   43,
          27,   32,   58,   62,   80,   67,   26,   44,
          -5,   19,   26,   36,   17,   45,   61,   16,
         -24,  -11,    7,   26,   24,   35,   -8,  -20,
         -36,  -26,  -12,   -1,    9,   -7,    6,  -23,
         -45,  -25,  -16,  -17,    3,    0,   -5,  -33,
         -44,  -16,  -20,   -9,   -1,   11,   -6,  -71,
         -19,  -13,    1,   17,   16,    7,  -37,  -26
    },
    { // Queen
         -28,    0,   29,   12,   59,   44,   43,   45,
         -24,  -39,   -5,    1,  -16,   57,   28,   54,
         -13,  -17,    7,    8,   29,   56,   47,   57,
         -27,  -27,  -16,  -16,   -1,   17,   -2,    1,
          -9,  -26,   -9,  -10,   -2,   -4,    3,   -3,
         -14,    2,  -11,   -2,   -5,    2,   14,    5,
         -35,   -8,   11,    2,    8,   15,   -3,    1,
          -1,  -18,   -9,   10,  -15,  -25,  -31,  -50
    },
    { // King
         -65,   23,   16,  -15,  -56,  -34,    2,   13,
          29,   -1,  -20,   -7,   -8,   -4,  -38,  -29,
          -9,   24,    2,  -16,  -20,    6,   22,  -22,
         -17,  -20,  -12,  -27,  -30,  -25,  -14,  -36,
         -49,   -1,  -27,  -39,  -46,  -44,  -33,  -51,
         -14,  -14,  -22,  -46,  -44,  -30,  -15,  -27,
           1,    7,   -8,  -64,  -43,  -16,    9,    8,
         -15,   36,   12,  -54,    8,  -28,   24,   14
    }
};

static const int16_t pst_eg[6][64] = {
    { // Pawn
           0,    0,    0,    0,    0,    0,    0,    0,
         178,  173,  158,  134,  147,  132,  165,  187,
          94,  100,   85,   67,   56,   53,   82,   84,
          32,   24,   13,    5,   -2,    4,   17,   17,
          13,    9,   -3,   -7,   -7,   -8,    3,   -1,
           4,    7,   -6,    1,    0,   -5,   -1,   -8,
          13,    8,    8,   10,   13,    0,    2,   -7,
           0,    0,    0,    0,    0,    0,    0,    0
    },
    { // Knight
         -58,  -38,  -13,  -28,  -31,  -27,  -63,  -99,
         -25,   -8,  -25,   -2,   -9,  -25,  -24,  -52,
         -24,  -20,   10,    9,   -1,   -9,  -19,  -41,
         -17,    3,   22,   22,   22,   11,    8,  -18,
         -18,   -6,   16,   25,   16,   17,    4,  -18,
         -23,   -3,   -1,   15,   10,   -3,  -20,  -22,
         -42,  -20,  -10,   -5,   -2,  -20,  -23,  -44,
         -29,  -51,  -23,  -15,  -22,  -18,  -50,  -64
    },
    { // Bishop
         -14,  -21,  -11,   -8,   -7,   -9,  -17,  -24,
          -8,   -4,    7,  -12,   -3,  -13,   -4,  -14,
           2,   -8,    0,   -1,   -2,    6,    0,    4,
          -3,    9,   12,    9,   14,   10,    3,    2,
          -6,    3,   13,   19,    7,   10,   -3,   -9,
         -12,   -3,    8,   10,   13,    3,   -7,  -15,
         -14,  -18,   -7,   -1,    4,   -9,  -15,  -27,
         -23,   -9,  -23,   -5,   -9,  -16,   -5,  -17
    },
    { // Rook
          13,   10,   18,   15,   12,   12,    8,    5,
          11,   13,   13,   11,   -3,    3,    8,    3,
           7,    7,    7,    5,    4,   -3,   -5,   -3,
           4,    3,   13,    1,    2,    1,   -1,    2,
           3,    5,    8,    4,   -5,   -6,   -8,  -11,
          -4,    0,   -5,   -1,   -7,  -12,   -8,  -16,
          -6,   -6,    0,    2,   -9,   -9,  -11,   -3,
          -9,    2,    3,   -1,   -5,  -13,    4,  -20
    },
    { // Queen
          -9,   22,   22,   27,   27,   19,   10,   20,
         -17,   20,   32,   41,   58,   25,   30,    0,
         -20,    6,    9,   49,   47,   35,   19,    9,
           3,   22,   24,   45,   57,   40,   57,   36,
         -18,   28,   19,   47,   31,   34,   39,   23,
         -16,  -27,   15,    6,    9,   17,   10,    5,
         -22,  -23,  -30,  -16,  -16,  -23,  -36,  -32,
         -33,  -28,  -22,  -43,   -5,  -32,  -20,  -41
    },
    { // King
         -74,  -35,  -18,  -18,  -11,   15,    4,  -17,
         -12,   17,   14,   17,   17,   38,   23,   11,
          10,   17,   23,   15,   20,   45,   44,   13,
          -8,   22,   24,   27,   26,   33,   26,    3,
         -18,   -4,   21,   24,   27,   23,    9,  -11,
         -19,   -3,   11,   21,   23,   16,    7,   -9,
         -27,  -11,    4,   13,   14,    4,   -5,  -17,
         -53,  -34,  -21,  -11,  -28,  -14,  -24,  -43
    }
};

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
            int mg = value_mg[p - 1] + pst_mg[p - 1][i];
            int eg = value_eg[p - 1] + pst_eg[p - 1][i];
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

// The Manhattan distance of a square from the four central squares,
// 0 to 6.
static int centre_distance(int sq)
{
    int f = file_of(sq), r = rank_of(sq);
    return (f < 4 ? 3 - f : f - 4) + (r < 4 ? 3 - r : r - 4);
}

// True if the side (0 black, 1 white) has only its king.
static int bare(int c)
{
    int n = 0;
    for (int i = 0; i < pos.count[c]; ++i)
        if (pos.pieces[c][i] && ++n > 1)
            return 0;
    return 1;
}

// With a bishop and a knight, the mate is only in a corner of the
// bishop's colour. True if the side (0 black, 1 white) has one of
// each, and then *dark is whether the bishop is on a dark square. At
// a phase of 2, where mop_up asks, they are its only pieces; it may
// have pawns too.
static int bishop_and_knight(int c, int *dark)
{
    int bishops = 0, knights = 0;
    for (int i = 0; i < pos.count[c]; ++i)
    {
        int sq = pos.pieces[c][i];
        int p = sq ? pos.board[sq] : 0;
        p = p < 0 ? -p : p;
        if (p == WB)
        {
            bishops++;
            *dark = !((file_of(sq) + rank_of(sq)) & 1); // a1 is dark
        }
        knights += p == WN;
    }
    return bishops == 1 && knights == 1;
}

// The mop-up term, from white's point of view: 0 unless one side has
// only its king. The phase is then the stronger side's alone.
__attribute__((noinline)) static int mop_up(void)
{
    int weak;
    if (bare(0))
        weak = 0;
    else if (bare(1))
        weak = 1;
    else
        return 0;
    int k = pos.king[weak], s = pos.king[!weak];
    int df = file_of(k) - file_of(s), dr = rank_of(k) - rank_of(s);
    int dist = (df < 0 ? -df : df) + (dr < 0 ? -dr : dr);
    int v, dark = 0;
    if (pos.phase == 2 && bishop_and_knight(!weak, &dark))
    {
        // The Manhattan distance of the bare king from the nearer
        // corner of the bishop's colour, 0 to 7: a1 or h8 if dark,
        // h1 or a8 if light.
        int f = file_of(k), r = rank_of(k);
        int c = dark ? f + r : 7 - f + r;
        if (c > 7)
            c = 14 - c;
        v = 40 * (7 - c) + 20 * (14 - dist);
    }
    else
        v = 10 * centre_distance(k) + 4 * (14 - dist);
    return weak ? -v : v;
}

int evaluate(void)
{
    int phase = pos.phase < 24 ? pos.phase : 24;
    int mg = unpack_mg(pos.psqt);
    int eg = unpack_eg(pos.psqt);
    int v = (mg * phase + eg * (24 - phase)) / 24;
    if ((unsigned) (phase - 2) <= 2) // 2 to 4
        v += mop_up();
    if (pos.side < 0)
        v = -v;
    return noise_amplitude ? v + noise() : v;
} // end of evaluate
