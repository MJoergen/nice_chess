/***************************************************************
 * internal.h: what the files of the engine core share.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef INTERNAL_H
#define INTERNAL_H

#include <assert.h>
#include "engine.h"

/***************************************************************
 * Squares
 *
 * The board is an array of 120 elements, 12 rows of 10 columns,
 * with a border of IV around the 8x8 squares. Square a1 is
 * element 21, h1 is 28, a8 is 91, and h8 is 98. A knight's move
 * from any square lands inside the array.
 ***************************************************************/

enum
{
    A1 = 21, B1 = 22, C1 = 23, D1 = 24, E1 = 25, F1 = 26, G1 = 27, H1 = 28,
    A8 = 91, B8 = 92, C8 = 93, D8 = 94, E8 = 95, F8 = 96, G8 = 97, H8 = 98
};

enum // Directions, for white. Black's pawns move by -N.
{
    N = 10, S = -10, E = 1, W = -1,
    NE = 11, NW = 9, SE = -9, SW = -11
};

static inline int file_of(int sq) { return sq % 10 - 1; } // 0 to 7
static inline int rank_of(int sq) { return sq / 10 - 2; } // 0 to 7
static inline int square(int file, int rank) { return rank * 10 + file + A1; }

/***************************************************************
 * Moves
 *
 * Bits  0- 6: from square
 * Bits  7-13: to square
 * Bits 14-17: the piece that moves, plus 6
 * Bits 18-21: the piece captured, plus 6 (6 is EM, no capture)
 * Bits 22-25: the piece promoted to, plus 6 (6 is EM, none)
 * Bits 26-27: the kind of move, one of the MK_ values
 ***************************************************************/

enum { MK_NORMAL = 0, MK_EP = 1, MK_CASTLE = 2, MK_DOUBLE = 3 };

static inline move_t move_new(int from, int to, int piece, int captured, int promoted, int kind)
{
    return from | to << 7 | (piece + 6) << 14 | (captured + 6) << 18
                | (promoted + 6) << 22 | kind << 26;
}

static inline int move_from(move_t m)     { return m & 0x7F; }
static inline int move_to(move_t m)       { return (m >> 7) & 0x7F; }
static inline int move_piece(move_t m)    { return ((m >> 14) & 0xF) - 6; }
static inline int move_captured(move_t m) { return ((m >> 18) & 0xF) - 6; }
static inline int move_promoted(move_t m) { return ((m >> 22) & 0xF) - 6; }
static inline int move_kind(move_t m)     { return (m >> 26) & 3; }

/***************************************************************
 * The position
 ***************************************************************/

enum // Castling rights
{
    CASTLE_WK = 1, CASTLE_WQ = 2, CASTLE_BK = 4, CASTLE_BQ = 8
};

// The longest game, in ply, including the plies of a search.
#ifndef MAX_HISTORY
#define MAX_HISTORY 512
#endif

// What make_move saves, so that undo_move can restore it.
typedef struct
{
    move_t   move;
    int32_t  state;  // castle | ep << 4 | halfmove << 11
    uint32_t hash;
    int32_t  slot;   // The place of the captured piece in its list
} history_t;

typedef struct
{
    int32_t   board[120];
    int32_t   side;      // 1 for white, -1 for black
    int32_t   castle;    // CASTLE_ bits
    int32_t   ep;        // The square behind a pawn that just moved two, or 0
    int32_t   halfmove;  // Plies since the last capture or pawn move
    int32_t   fullmove;  // The move number, from 1
    int32_t   psqt;      // Material and piece-square values, packed (eval.c)
    int32_t   phase;     // The material left, from 24 at the start to 0
    uint32_t  hash;      // Zobrist key
    int32_t   king[2];   // The squares of the kings: [0] black, [1] white
    int32_t   ply;       // The number of entries of hist

    // The piece lists: the squares of the pieces of each side, [0]
    // black and [1] white, in the order in which they were put on
    // the board, and the place in its list of the piece on each
    // square (an empty square's is stale). A captured piece keeps its
    // place, with square 0, which is off the board, until undo_move
    // puts it back; so count is the number of places in use.
    int32_t   pieces[2][16];
    int32_t   count[2];
    int32_t   index[120];
} position_t;

extern position_t pos;
extern history_t hist[MAX_HISTORY];

static inline int king_of(int side) { return pos.king[side > 0]; }

// p*side, for side 1 or -1, without a multiplication. NICE has no
// multiply instruction: the translator multiplies by a variable, or
// by some negative constants, on the math unit, which costs about
// six instructions.
static inline int for_side(int p, int side)
{
    int m = -(side < 0);
    return (p ^ m) - m;
}

// board.c
extern uint32_t zob_piece[13][64];      // By piece + 6 and sq64
extern uint32_t zob_castle[16];
extern uint32_t zob_ep[8];              // By file
extern uint32_t zob_side;               // Black to move
extern int32_t sq64[120];
void board_init(void);
void board_clear(void);
void board_put(int sq, int piece);
uint32_t board_compute_hash(void);
int  board_consistent(void);
int  is_attacked(int sq, int side);     // By the side opposite to side

/***************************************************************
 * The move stack
 *
 * The search and perft keep their move lists on one global stack.
 * Each level generates onto the top, from move_sp, and moves
 * move_sp past its moves until it returns.
 ***************************************************************/

#ifndef MOVE_STACK_SIZE
#define MOVE_STACK_SIZE 1024
#endif

extern move_t move_stack[MOVE_STACK_SIZE];
extern int32_t move_sp;

// True if one more list fits on the move stack.
static inline int move_stack_room(void)
{
    return move_sp <= MOVE_STACK_SIZE - ENGINE_MAX_MOVES;
}

// movegen.c
int gen_moves(move_t *list, int quiet); // Pseudo-legal moves, returns count
int gen_legal(move_t *list);
static inline int gen_pseudo(move_t *list)   { return gen_moves(list, 1); }
static inline int gen_captures(move_t *list) { return gen_moves(list, 0); }

// makemove.c
void make_move(move_t m);
void undo_move(void);
int  move_was_legal(void);              // After make_move: the mover's king is safe
void make_null_move(void);              // Passes the move (the search's null move)
void undo_null_move(void);

// eval.c
extern int32_t psqt[13][64];            // By piece + 6 and sq64, packed and signed
extern const int32_t phase_value[13];   // By piece + 6
void eval_init(void);
int  evaluate(void);                    // From the point of view of the side to move

// tt.c: the transposition table, of 1 << TT_BITS entries of 8 bytes,
// 128 KB. The NICE builds' linear memory (NICEFLAGS) must hold it.
#ifndef TT_BITS
#define TT_BITS 14
#endif

enum { TT_UPPER = 1, TT_LOWER = 2, TT_EXACT = 3 }; // The kind of bound

void tt_clear(void);
void tt_store(int depth, int value, int bound, move_t move, int ply);
int  tt_probe(int *depth, int *value, int *bound, int *move, int ply);
int  tt_match(const move_t *list, int n, int short_move);

#endif // INTERNAL_H
