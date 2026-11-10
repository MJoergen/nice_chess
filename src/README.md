# src

- `engine/`: the engine core. `engine.h` is its interface; `internal.h`
  is what its files share. It does no I/O, reads no clock, and calls no
  malloc, so it builds for NICE and for the host alike.
  - `board.c`: the position, with a list of the squares of each
    side's pieces, the Zobrist keys, and attacks.
  - `movegen.c`: move generation, and the global move stack.
  - `makemove.c`: make and undo, which keep the evaluation's sums, the
    king squares, the piece lists, and the Zobrist key up to date, and
    the null move.
  - `fen.c`: FEN input and output, and moves in coordinate notation.
  - `eval.c`: the evaluation, by material and piece-square tables.
  - `search.c`: the search, by iterative deepening, with a principal
    variation search, null-move pruning, late move reductions,
    quiescence, and move ordering with a history table.
  - `tt.c`: the transposition table.
  - `perft.c`: perft.
  - `game.c`: the game as the front ends see it, and its result.
  - `book.c`: the opening book, a table sorted by the Zobrist key.
    `book.txt` has its lines, from which `tools/mkbook.c` makes the
    table, `build/gen/book_data.c`, with the engine's own keys.
- `platform/`: the clock, the default time of a move, the rate of the
  players' clocks, and reads of the console, a byte at a time, which
  wait or not: `nice.c` for NICE, `host.c` for a PC, behind
  `platform.h`. On NICE the time of a
  move is 300 million steps in niceemu and 5 seconds on a board, which
  `nice.c` tells apart by the ratio of the cycle counter to the
  microsecond counter. The players' clocks count 54 million steps as a
  second in niceemu, the MEGA65's rate, and real time on a board.
- `ui/version.h`: the version the front ends show, from
  `build/gen/version.c`, which the Makefile writes (`VERSION`).
- `ui/text.c`: the text front end, on the console, and UCI. `help` lists
  its commands. During a search it reads the console, every 1024 nodes,
  for stop, and for ponderhit after `go ponder`.
- `ui/screen.c`: the windowed front end, on the 80x25 screen, for NICE
  only. Its keys are listed at the top of the file and on the screen.
  It copies the character ROM into the font RAM, writes the pieces over
  the codes 0x80 to 0xEB, and shows the font RAM (nice_cpu's SYSTEM.md
  §1.8). Each piece is 3x3 glyphs: a white piece its outline, a black
  piece filled. `ui/pieces.h` has the glyphs, which
  `tools/mkpieces.py` draws (`make pieces`).
  It ponders on the player's time: the search of the position after
  the move it expects reads the keys, and draws from a copy of the
  position, as the engine's board is then in the search.
  It writes its log of the moves to the console's register itself, so
  that under the monitor on the MEGA65 the log goes to the line and
  not to the screen.

Both front ends are built twice for NICE: for niceemu, at address 0,
and for the MEGA65 under NiceMonMEGA65's monitor, at `0x8000`
(`nicebuild.py --monitor`), where the text front end's output goes to
the screen and the line both, through the monitor.

The engine started as `runtime/libc/test/mchess.c` of
[nice_cpu](https://github.com/MJoergen/nice_cpu) at tag `V1.2`, by the
same author, under the MIT license. Castling, en passant, FEN, and the
Zobrist keys follow [mchess2](https://github.com/MJoergen/mchess2) at
commit `65e8855`, which is not copied here.
