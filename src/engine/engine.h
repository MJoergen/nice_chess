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

// Starts a new game from the opening position.
void engine_new_game(void);

// Starts a new game from a FEN string. The half-move clock and the
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

// Called by engine_search once for each root move, with its score
// in centipawns from the point of view of the side to move.
typedef void (*engine_info_fn)(move_t move, int score);

// Searches the position to a fixed depth in ply, and returns the best
// move, or 0 if there is no legal move. info may be NULL.
move_t engine_search(int depth, engine_info_fn info);

#endif // ENGINE_H
