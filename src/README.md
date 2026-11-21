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
  - `eval.c`: the evaluation, by material and piece-square tables,
    and a mop-up term against a bare king.
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
  players' clocks, reads of the console, a byte at a time, which
  wait or not, and writes: `nice.c` for NICE, `host.c` for a PC, behind
  `platform.h`, and a UDP endpoint for remote play: a socket in `host.c`,
  and in `nice.c`, built with `-D NICE_NET`, the monitor's UDP entries,
  through the stubs of `net.nasm`.
  On NICE the time of a
  move is 300 million steps in niceemu and 5 seconds on a board, which
  `nice.c` tells apart by the ratio of the cycle counter to the
  microsecond counter. The players' clocks count 54 million steps as a
  second in niceemu, the MEGA65's rate, and real time on a board.
- `ui/version.h`: the version the front ends show, from
  `build/gen/version.c`, which the Makefile writes (`VERSION`).
- `ui/text.c`: the text front end, on the console, and UCI, and
  remote play (`remote`), with `ui/remote.c`. `help` lists
  its commands. On the host, `tools/remote_talk.py` runs two of them
  in a remote game. During a search it reads the console, every 1024 nodes,
  for stop, and for ponderhit after `go ponder`. A search with no limit
  also ends when another line comes, such as quit, and at the end of
  the input.
- `ui/mini.c`: the minimal front end, for the Nexys4DDR's 26K words.
  It reads a line of the console, and writes the reply of
  `ui/miniuci.c`, which runs a subset of UCI, a line in and a reply
  out, with no I/O and no printf. A front end on the network can take
  mini.c's place.
- `ui/remote.c`: remote play (#27), a game against another board over
  UDP, through the platform's network. `remote.h` specifies the
  protocol. It sends each message again until its ACK comes, ignores
  the repeats, and checks the other side's moves and FEN against the
  engine.
- `ui/peer.c`: a player of remote play whose engine plays its side. It
  answers a NEW, or sends one, and prints the moves of the game. It is
  built for the host, for the Nexys4DDR (`build/nexys/chess_peer.bin`),
  and for the MEGA65 (`build/mega65/chess_peer.bin`).
  On the host, `tools/remote_test.py` plays two of them against each
  other over the loopback.
- `ui/screen.c`: the windowed front end, on the 80x25 screen, for NICE
  only, and remote play by its keys, with `ui/remote.c`. Its keys are
  listed at the top of the file and on the screen.
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
the screen and the line both, through the monitor. The minimal front
end is built for niceemu, and for the Nexys4DDR under the monitor.

The engine started as `runtime/libc/test/mchess.c` of
[nice_cpu](https://github.com/MJoergen/nice_cpu) at tag `V1.2`, by the
same author, under the MIT license. Castling, en passant, FEN, and the
Zobrist keys follow [mchess2](https://github.com/MJoergen/mchess2) at
commit `65e8855`, which is not copied here.
