/***************************************************************
 * miniuci.h: the game of the minimal build, as a subset of UCI,
 * one line in and one reply out, with no I/O of its own.
 *
 * The console front end, mini.c, reads the lines and writes the
 * replies. A front end on the network can take its place.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef MINIUCI_H
#define MINIUCI_H

#include <stdint.h>

// Starts a game from the opening position. The seed is that of the
// opening book's choices.
void mini_init(uint32_t seed);

// Runs one command, a line without its end of line, which it may
// change. Returns the reply, of zero or more lines, each ending in
// "\n", and valid until the next call, or NULL after quit.
//
//   uci                         id, and uciok
//   isready                     readyok
//   ucinewgame                  a new game
//   position startpos|fen FEN [moves M...]
//   go [nodes N]                bestmove, from the book where the
//                               position is in it, or a search of N
//                               nodes (MINI_NODES by default)
//   quit
//
// Any other line is ignored, as UCI asks. Where the game has ended,
// go replies "info string result R REASON" and "bestmove 0000".
const char *mini_command(char *line);

#endif
