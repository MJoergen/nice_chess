/***************************************************************
 * engine.h: the interface of the chess engine core.
 *
 * The core keeps one game: the position, and the history of the
 * moves that led to it. It does no I/O, reads no clock, and calls
 * no malloc, so the same code builds for NICE and for the host.
 *
 * A move is one 32-bit word (move_t). The value 0 is no move.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

typedef int32_t move_t;

// The contents of a square, as engine_piece_at returns them.
enum
{
    EM = 0,                                         // Empty
    WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6, // White
    BP = -1, BN = -2, BB = -3, BR = -4, BQ = -5, BK = -6, // Black
    IV = 99                                         // Off the board
};

// The most moves a position can have. 218 is the known maximum.
#define ENGINE_MAX_MOVES 256

// The longest FEN string engine_get_fen writes, with the final zero.
#define ENGINE_FEN_MAX 96

// The result of engine_result.
enum
{
    ENGINE_PLAYING    = 0,
    ENGINE_CHECKMATE  = 1, // The side to move is mated
    ENGINE_STALEMATE  = 2,
    ENGINE_FIFTY      = 3, // A draw by the 50-move rule
    ENGINE_REPETITION = 4  // A draw by threefold repetition
};

// The opening position.
#define ENGINE_START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

// Starts a new game from the opening position, and forgets what
// earlier searches learned (the transposition table).
void engine_new_game(void);

// Starts a new game from a FEN string, but keeps what earlier
// searches learned, as a series of positions of one game sends them. The half-move clock and the
// move number may be left out. Returns 0 on success, and -1 if the
// FEN is not valid, in which case the position is unchanged. If end
// is not NULL, it gets a pointer to the first character after the FEN.
int engine_set_fen(const char *fen, const char **end);

// Writes the position as FEN to buf, at least ENGINE_FEN_MAX bytes.
void engine_get_fen(char *buf);

// The piece on a square, file and rank 0 to 7 (0, 0 is a1).
int engine_piece_at(int file, int rank);

// 1 if white is to move, -1 if black is.
int engine_side_to_move(void);

// True if the side to move is in check.
int engine_in_check(void);

// Writes the legal moves to out, at least ENGINE_MAX_MOVES entries,
// and returns their number.
int engine_legal_moves(move_t *out);

// Parses a move in coordinate notation, such as "e2e4" or "e7e8n",
// and returns the legal move it names, or 0. A promotion with no
// piece given is to a queen. If end is not NULL, it gets a pointer to
// the first character after the move.
move_t engine_parse_move(const char *s, const char **end);

// Writes a move in coordinate notation to buf, at least 6 bytes.
void engine_move_to_str(move_t move, char *buf);

// The squares a move goes from and to, as file + 8 * rank (0 is a1,
// 63 is h8).
int engine_move_from(move_t move);
int engine_move_to(move_t move);

// Makes a legal move. Returns 0, or -1 if the history is full.
int engine_make_move(move_t move);

// Takes back the last move. Returns 0, or -1 if there is none.
int engine_undo(void);

// Whether the game has ended, and how (ENGINE_PLAYING if not).
int engine_result(void);

// The number of leaf nodes of the tree of legal moves of the given depth.
uint32_t engine_perft(int depth);

// The Zobrist key of the position, which make and undo keep up to date.
uint32_t engine_hash(void);

// The Zobrist key of the position, computed from scratch.
uint32_t engine_hash_full(void);

/***************************************************************
 * The opening book (book.c, and the lines of book.txt)
 ***************************************************************/

// Writes the moves the book has for the position to moves, and their
// weights to weights, at least ENGINE_MAX_MOVES entries each, and
// returns their number: 0 if the position is not in the book.
int engine_book_moves(move_t *moves, uint32_t *weights);

// A move of the book for the position, chosen at random in proportion
// to the weights, or 0 if the position is not in the book.
move_t engine_book_move(void);

// Seeds the random choice of engine_book_move. The same seed gives the
// same choices.
void engine_book_seed(uint32_t seed);

/***************************************************************
 * The search
 ***************************************************************/

// Scores are in centipawns, from the point of view of the side to
// move. A score above ENGINE_MATE - ENGINE_MAX_PLY is a mate in
// ENGINE_MATE - score plies, and one below -(ENGINE_MATE -
// ENGINE_MAX_PLY) is being mated in ENGINE_MATE + score plies.
#define ENGINE_MATE    30000
#define ENGINE_MAX_PLY 64      // The deepest the search goes, in ply

// The longest principal variation that engine_info_t holds.
#define ENGINE_MAX_PV  16

// What limits a search. A field of 0 sets no limit. A search that a
// limit stops still returns a legal move: if it stops during depth 1,
// the first in the order of the moves (the move of the transposition
// table, or the best capture).
typedef struct
{
    int      depth;     // The deepest iteration, in ply
    uint32_t nodes;     // The most nodes to visit
    int    (*stop)(void); // Called every 1024 nodes; nonzero stops the search
} engine_limits_t;

// What the search found in an iteration of the iterative deepening.
typedef struct
{
    int      depth;
    int      score;
    uint32_t nodes;     // From the start of the search
    int      pv_len;
    move_t   pv[ENGINE_MAX_PV]; // The principal variation, from the root
} engine_info_t;

// Called by engine_search after each iteration that it completes.
// A nonzero return stops the search. This is where a front end
// decides that it is not worth starting another iteration.
typedef int (*engine_info_fn)(const engine_info_t *info);

// Searches the position by iterative deepening, until a limit is
// reached, and returns the best move, or 0 if there is no legal
// move. info may be NULL. The position is unchanged afterwards.
move_t engine_search(const engine_limits_t *limits, engine_info_fn info);

// The time to take for a move, under a clock with left on it, inc
// added after each move, and movestogo moves to the next time control
// (0 if none): a share of the time left, and most of the increment,
// but never more than half of the time left. Any unit, the same for
// all.
uint32_t engine_time_share(uint32_t left, uint32_t inc, uint32_t movestogo);

#endif // ENGINE_H
