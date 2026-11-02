# Porting plan: from mchess2 to a chess engine on NICE

This is the plan for turning [mchess2](https://github.com/MJoergen/mchess2), a
C++ chess engine for a host PC, into a playable chess program for the NICE
system. Development targets `niceemu` from
[nice_cpu](https://github.com/MJoergen/nice_cpu) first, then the
[NiceMonMEGA65](https://github.com/MJoergen/NiceMonMEGA65) core. There are two
front ends, a text-only one on the console and a windowed one on the 80×25
character screen.

The survey behind it used mchess2 at `65e8855` (Nov 2017), nice_cpu at
`68323dc` (V1.2), and NiceMonMEGA65 `main` as of 2026-11-02. The figures
below come from those repositories' documents and comments. None of them
were measured for this plan.

## Summary

1. **Reference, don't copy.** mchess2 stays in its own repository,
   archived, and nice_chess keeps no copy of it: no `git subtree` (its
   history carries a 29 MB `book.bin`, which no code reads, and a Windows
   `mchess.exe`) and no `ref/` directory. Where nice_chess needs something
   from it, such as the test suites, it names commit `65e8855` and fetches
   the file from there.
2. **Rewrite in C, using `mchess.c` as the base.** The NICE toolchain
   compiles C through clang and wasm32, and it has no C++ standard library.
   mchess2 depends on `std::vector`, `std::string`, and iostreams
   throughout. nice_cpu already has `runtime/libc/test/mchess.c`, a
   single-file C version of an earlier mchess, which builds and runs on
   `niceemu` today. Start from it, and bring over mchess2's extra features
   one at a time: castling, en passant, the transposition table, iterative
   deepening, time control, and UCI.
3. **Memory decides the design, not speed.** On the MEGA65, a program under
   the monitor gets 28K words (112 KB) for its code, data, and stack. Wasm
   linear memory alone takes at least one page, 16K words. The current
   `mchess.c` build uses four pages, which is the whole of the board's RAM.
4. **Speed comes second, and it is also a real limit.** `mchess.c` takes
   about 10¹⁰ steps for one depth-5 search from the opening position. At
   100 MHz and the pipelined core's 1.43 cycles per instruction, that is
   about 2.5 minutes per move. The evaluation function has to be cheap, and
   the search has to be stopped by a clock.
5. **Split the program into an engine core and front ends** (see
   Architecture). The core builds for the host as well, so the move
   generator can be checked against perft suites at host speed.

## What exists today

### mchess2 (C++, host PC)

About 4300 lines of C++:

| File | Contents | Bring over? |
|---|---|---|
| `CSquare.h`, `CMove.*`, `CMoveList.*` | 10×12 mailbox squares, the move type, the move list (a `std::vector`) | Yes, as C structs and fixed arrays |
| `CBoard.*` (1524 lines) | Board, FEN reading, legal move generation with castling and en passant, make/undo, evaluation | Yes, the largest part of the port |
| `CHashEntry.*`, `CHashTable.*` | Zobrist keys (781 × 64 bits) and the transposition table (8M entries × 16 bytes = 128 MB) | Yes, with 32-bit keys and a table of a few thousand entries |
| `ai.*` | Negamax alpha-beta with a transposition table, quiescence search, killer move, iterative deepening, time control | Yes |
| `CPerft*`, `CSearchSuite*` | Perft and search test drivers | Yes, as host-side tests |
| `main.cc` | Console UI and UCI | Rewrite, as front ends |
| `CTime.h`, `trace.h`, `crc32.h` | `clock()` wrapper, `TRACE` macro, CRC | Replace with small C equivalents |
| `tests/perftsuite.epd` (126 positions), `tests/searchsuite.epd` (564), `tests/*.fen` | Test positions | Fetched from `65e8855` by the tests, not copied |
| `doc/` (UCI spec, theory PDF) | Reference | Link to them, don't copy |
| `book.bin`, `mchess.exe` | Unused opening book, Windows binary | No |

Known weak spots, from its own TODO and the code:

- `get_value()` runs the full legal move generator twice for each leaf, to
  count mobility. On NICE this would be most of the run time.
- There is no detection of draws by repetition or by the 50-move rule.
- Each search level copies a `CMoveList`, and the principal variation is
  built by concatenating lists.

### mchess.c in nice_cpu (C, already runs on niceemu)

`runtime/libc/test/mchess.c` is 987 lines, MIT, by the same author. It has
the same 10×12 board, alpha-beta at a fixed depth of 5 with quiescence, a
console UI, and `perft`. It has no castling, en passant, transposition
table, time control, or UCI. It is a test of nice_cpu's C library, so it
will stay there. We copy it into `src/` as the start of our own code (it
is MIT, by the same author), and don't depend on nice_cpu's copy.

What its build says about the platform (from `runtime/libc/Makefile`):

- Each search level puts a move list of 2.5 KB on the wasm stack. Five
  levels plus up to 30 levels of quiescence came to about 100 KB, so it
  links with a 128 KB stack and four pages of memory (256 KB).
- `perft 3` takes 11 million steps, and a `go` from the opening takes about
  10¹⁰.

## Constraints of the platform

### Language and toolchain

- No compiler targets NICE directly yet (nice_cpu issue #66). C, Zig, and
  Rust reach NICE as wasm32 modules, which `nicewasm.py` translates into
  NICE assembly. `translator/nicebuild.py` does all the steps:
  `nicebuild.py -o chess.bin chess.c`.
- The C library is small: `printf`, `scanf`, `string.h`, `malloc`,
  `ctype.h`, `assert`, `clock()`, and the `nice.h` device calls. There is no
  file I/O, so test positions have to come in on the console or be compiled
  in.
- C++ could in principle be built with clang for wasm32 if it used no
  standard library, no exceptions, and no RTTI. That would mean rewriting
  every container anyway, and nicebuild does not support it. **C is the
  choice.**
- Hot routines can later be written by hand in NICE assembly and joined
  with `nicebuild.py --asm FILE`.
- Host tools: Python 3.10+, clang with the wasm32 target, `wasm-ld`, and
  `llvm-ar`. niceemu comes from the V1.2 release, with or without the
  window, so no Zig is needed.

### Memory

| Target | Room for the program | Notes |
|---|---|---|
| niceemu | Practically unlimited (2³² words) | It is easy to write a program here that never fits on the board. |
| NiceMonMEGA65, under the monitor | `0x8000`–`0xEFFF`: **28K words**, for the image and the NICE stack | Loaded over the debug link (UART or Ethernet). The monitor cannot load programs from the board by itself yet. |
| NiceMonMEGA65, in the ROM | 32K words | A C program cannot be a ROM, because its data would be in ROM too. |

Wasm linear memory is a whole number of 64 KB pages, and one page is 16K
words. So on the board, **code, runtime, and libc must fit in about 12K
words minus the NICE stack**, with every byte of data in a single 64 KB
page: the wasm stack, the board, the move stack, the transposition table,
and the strings. For scale, nice_cpu's `primes.c` example with `printf`
and `scanf` ends at word `0x631F` when loaded at 0. That is about 9K words
beyond its one page. The first task of Phase 1 is to measure the
real size of the engine.

So, from the start:

- Use one global move stack, for example 1024 moves of 4 bytes, indexed
  by ply. No per-level `MoveList` on the stack.
- Use 32-bit Zobrist keys, and a transposition table of 2K to 4K entries
  of 8 bytes (16 to 32 KB). The table's size is a compile-time constant
  per target.
- Avoid `printf` in the core. Front ends can use small print routines of
  their own if `printf` turns out to cost too much code.
- Keep a `make size` target that reports where the image ends and how
  much of the linear memory is used, and fail the board build if it does
  not fit.

If the budget can't be met, there are two ways out. Both are open
questions, and neither is needed for niceemu:

1. Use more of the board's RAM. The MEGA65 has HyperRAM, but
   NiceMonMEGA65 doesn't map it today.
2. Write the move generator and make/undo by hand in NICE assembly. This
   is smaller and faster than translated wasm.

### Speed

- Translated code: a wasm byte or halfword store becomes a read, a
  change, and a write of the word. An `int64` becomes two words, and
  operations on it become multi-word sequences. Keep the board, the move
  fields, and the counters in `int32_t` (one word each), and keep hash
  keys at 32 bits.
- The MEGA65 runs at 100 MHz. The pipelined core measures 1.43 cycles per
  instruction on nice_cpu's benchmark, so it runs about 70M steps per
  second. In niceemu, a step is one microsecond of `clock()`, so time
  controls in the emulator are counted in steps, not wall-clock time.
- Use evaluation by material plus piece-square tables, updated in
  make/undo. Drop the mobility evaluation of mchess2. Use iterative
  deepening with a time or node budget, MVV-LVA capture ordering, the move
  from the transposition table first, and killer moves.
- The aim is a move in 5 to 15 seconds on the board, at whatever depth
  that reaches (probably 4 to 5 ply). That is fine for a demo.

### I/O

- **Console:** `getchar`/`printf` (or `nice_getc`/`nice_pollc`/`nice_putc`).
  In niceemu the console is stdin and stdout, or a file through `-i`. On
  the board it is the UART or Ethernet, and the MEGA65 keyboard feeds the
  same input.
- **Screen:** 80×25 character cells at `0xFFFF0000`, one word each:
  ISO 8859-1 code in bits 7..0, foreground color in bits 19..8, and
  background color in bits 31..20 (4 bits each of R, G, and B). There is
  a hardware cursor. There are no bitmap graphics and no user-defined
  glyphs, so pieces are letters or Latin-1 symbols on colored squares.
- **Keyboard:** in a window, the keys come in through the console. A read
  returns 0 when no key waits, which lets the engine poll for keys during
  its search.
- niceemu `--window` (the windowed build of the release) shows the same
  screen, so the windowed front end can be developed entirely in the
  emulator. `niceemu --color -i keys.txt` prints the final screen, which
  makes the windowed front end testable in CI without a display.

## Architecture

```
src/engine/   board.c  movegen.c  makemove.c  eval.c  search.c  tt.c
              perft.c  fen.c  game.c  (the core: no I/O, no clock, no malloc)
              engine.h            (its interface)
src/platform/ nice.c   host.c     (time, and from Phase 3 the poll for a key)
src/ui/       text.c              (console: board diagram, coordinate input, and UCI)
              screen.c            (windowed: board on the 80x25 screen, keyboard cursor)
tests/        perft and search suites, and the expected outputs of niceemu runs
tools/        the test runner, the size report, and a match runner for UCI engines
```

The core's API is small and has no state outside it. For example:
`engine_new_game`, `engine_set_fen`, `engine_legal_moves`,
`engine_make_move`, `engine_undo`, `engine_search(limits, callback)`,
`engine_result` (mate, stalemate, 50 moves, repetition), and `engine_perft`.
The search calls a platform hook every N nodes, to check the clock and the
keyboard. That is how the windowed front end stays responsive and lets the
player stop a search.

Both front ends are separate `main`s that link the same core, so there are
two `.bin` files, `chess_text.bin` and `chess_screen.bin`. The text front
end is the one in mchess2's `main.cc`: `show`, `move e2e4`, `go`, `perft
N`, `fen ...`, `undo`, and a `uci` mode. UCI over the console lets host
GUIs and cutechess play the engine running inside niceemu.

The core also builds with the host's clang or gcc. Perft suites run in
seconds there, and a smaller depth of them runs on niceemu.

## Phases

Each phase ends with something that runs and with tests in CI.

**Phase 0: the repository (small).**
- Copy `mchess.c` from nice_cpu V1.2 into `src/`, unchanged, and note
  where it came from. Mark mchess2 as archived on GitHub.
- Write a Makefile that clones nice_cpu at a pinned release tag
  (`NICE_CPU_TAG = V1.2`) into `deps/`, and downloads niceemu of the same
  release. `NICE_CPU=DIR` and `NICEEMU=PATH` point to local copies instead.
  Add a GitHub Actions job that installs clang and lld and runs
  `make test`.
- Exit criterion: `mchess.c`, unchanged, builds and runs `perft 3` on niceemu
  in CI.

**Phase 1: the engine core in C, measured.**
- Restructure `mchess.c` into `src/engine/` with the API above. Use the
  global move stack, `int32_t` everywhere, and 32-bit Zobrist keys.
- Add castling, en passant, under-promotion, and FEN input/output from
  mchess2's `CBoard.cc`.
- Fetch the perft suite from mchess2 at `65e8855`, and run it on the
  host (all 126 positions to depth 4 or 5), and a few positions to depth
  3 on niceemu.
- Add `make size`, and record code words, data bytes, and peak stack.
- Exit criterion: perft matches on the host and on niceemu, and there is a
  first size figure against the 28K-word budget.

**Phase 2: search, playable in text on niceemu.**
- First, two changes that Phase 1's measurements call for (Progress below,
  and [LOG.md](../LOG.md) of 2026-11-02):
  1. Captures in the quiescence search ordered by MVV-LVA. In the order
     they are generated, `go 1` on Kiwipete takes more than 2·10⁹ steps.
  2. A material plus piece-square table evaluation in place of
     mchess.c's mobility, which generates the moves of both sides at
     every leaf: 79% of the steps of `go 3` are in `gen_pseudo`.
- Then iterative deepening, the transposition table, the rest of the
  move ordering, a time or node limit, and detection of repetition and
  the 50-move rule.
- The text front end with the board diagram, move input in coordinate
  notation, `go`, `undo`, and game end. Add UCI after that.
- Run a subset of the search suite (fetched like the perft suite) on the
  host. On niceemu, run a few fixed-node searches whose best move and
  node count are checked.
- Exit criterion: a full game against the engine in `niceemu`, in text,
  with each reply in a reasonable step budget, plus a host build that beats
  random moves and plays mchess2 to a sensible result under cutechess.

**Phase 3: the windowed front end on niceemu.**
- `screen.c`: a board of 8×8 squares, 5 columns × 3 rows of cells each
  (40×24, which leaves 40 columns on the right for the move list, clocks,
  the search info, and help), light and dark square colors, and piece
  letters in two colors. Move by cursor keys and Enter, or by typing
  `e2e4`. Highlight the last move and legal targets. Show "thinking" while
  the search runs, and allow Esc to make the engine move now.
- Test it with `niceemu --color -i keys.txt`, comparing the screen.
- Exit criterion: a full game played in `niceemu --window`.

**Phase 4: NiceMonMEGA65.**
- Build with `nicebuild.py --monitor`, which loads at `0x8000`. Load and
  run over the debug link (`load.py` / `run.py` in NiceMonMEGA65).
- Get it to fit: tune the table sizes, trim libc use, and if needed move
  hot routines into hand-written NICE assembly, or use more RAM if
  NiceMonMEGA65 provides it by then (see Decisions).
- Tune the time limits for the 100 MHz clock (`clock()` there is real
  microseconds).
- Exit criterion: a full game on the MEGA65 screen and keyboard, and the
  text version over the UART or Ethernet.

**Later, optional.** An opening book small enough to compile in (a few
hundred positions), a better evaluation (pawn structure, king safety),
PVS or aspiration windows, SAN move input and output, a ROM-able build once there is a direct NICE C
compiler or a monitor that loads from SD card, and custom chess glyphs if
NiceMonMEGA65's font ever becomes programmable.

## Progress

**Phase 0** is done (2026-11-02): the Makefile, the pins, and CI.

**Phase 1** is done (2026-11-02):

- The core is in `src/engine/`, with the interface of `engine.h`. A move
  is one 32-bit word. The board, the state, and the counters are
  `int32_t`. Move lists are on one global move stack of 1024 moves, and
  the history of the game, 512 plies of 12 bytes, keeps what undo needs
  and the Zobrist key of each position, for the detection of repetition.
- Castling, en passant, under-promotion, FEN input and output, and
  32-bit Zobrist keys, kept up to date in make and undo. The game result
  (mate, stalemate, the 50-move rule, and threefold repetition) is in
  already. The search and the evaluation are still those of `mchess.c`.
- The perft suite, all 126 positions, matches on the host to depth 4 in
  `make test` and to depth 5 (396 million nodes) in `make perft-deep`,
  with the Zobrist key checked against a full computation at every node.
  On niceemu, all 126 match to depth 3: 509 thousand nodes in 517
  million steps, about 7 seconds at the board's speed.
- Size, from `make size` (NICE build with `-Os` and without asserts):

  | Part | Words |
  |---|---|
  | Code, runtime, and libc | 14288 |
  | Linear memory, one page | 16384 |
  | Peak NICE stack | 188 |
  | **Total** | **30860**, which is 2188 over the budget of 28672 |

  In the linear memory, the wasm stack is 8 KB and peaks at about 2 KB,
  and the data segments take 880 bytes. The game history, the move
  stack, and the Zobrist keys are about 13.6 KB of zero-initialized data,
  so most of the page is still free.

  The largest parts of the code are `printf` (its `format` and `number`,
  3727 words), the text front end's `main` (1238), FEN input (1029) and
  output (811), move generation (914), and make and undo (1038). Without
  `printf`, the program would fit, so a small print routine of our own is
  the first step in Phase 4. `-O2` instead of `-Os` would be 20% faster
  and 2800 words larger.

Found on the way, for Phase 2: the quiescence search of `mchess.c` tries
the captures in the order they are generated. On a tactical position
such as Kiwipete (the second line of the perft suite), `go 1` visits 2.5
million quiescence nodes, which takes more than 2·10⁹ steps on niceemu.
Ordering captures by MVV-LVA is the fix, and the reason it comes first in
Phase 2.

**Phase 2** is done (2026-11-02):

- The evaluation is material plus piece-square tables (Michniewski's
  "Simplified Evaluation Function", with end-game tables for the king
  and the pawns), blended by the phase of the game. make and undo keep
  the middle-game and end-game sums up to date, packed in one word, so
  `evaluate` reads them and does no work of its own. perft on the host
  checks the sums, the phase, and the Zobrist key at every node.
- The search (`search.c`) is alpha-beta by iterative deepening, with a
  quiescence search of captures and promotions, a check extension,
  draws by repetition and the 50-move rule inside the tree, and a
  triangular table for the principal variation. The moves are ordered:
  the move of the transposition table, captures by MVV-LVA, promotions
  to a queen, two killer moves a ply, then the rest. The quiescence
  search uses its own generator of captures.
- The transposition table (`tt.c`) has 2048 entries of 8 bytes (16 KB),
  with the move in 16 bits and the value in 16, and the depth and
  bound in the low 8 bits of the stored key. A new entry always
  replaces the old.
- `engine_search` takes limits (depth, nodes, and a stop function
  that it calls every 1024 nodes) and calls the front end after each
  iteration, which can stop it. The time is the front end's: it stops
  the search at the hard limit, and starts no iteration after half of
  it. The platform gives the clock and the default time of a move:
  300 million steps on NICE (about 7 seconds in niceemu, 4 on the
  board), 1 second on the host.
- The text front end has `go` with UCI's limits (`go depth 5`,
  `go nodes 20000`, `go movetime 1000`, `go wtime ...`), shows the
  board after the engine's move, and ends the game at mate, stalemate,
  the 50-move rule, and repetition. After `uci`, it speaks UCI:
  `position`, `go`, and the rest, but it cannot hear `stop` during a
  search.
- From the opening, `go depth 5` took 10¹⁰ steps before and takes 46
  million now. The default `go` in niceemu reaches depth 5 from the
  opening, 4 in a Sicilian Dragon middle game, and 6 in a rook ending,
  at about 3000 steps a node. niceemu runs about 42 million steps a
  second on the PC used here.
- Tests: fixed searches (`tests/search.input`, by depth and by nodes)
  must print the same lines on the host and in niceemu, which they do
  since the search is deterministic. mchess2's search suite (fetched at
  `65e8855`) runs on the host at 10000 nodes a position, and its
  score is checked: 239 of 548, of which 154 of the 300 of Win at
  Chess (216 at 100000 nodes). `make match-random` plays 20 games
  against random moves at 20000 nodes a move, and must win all; it
  does, in about a second, and runs in CI.
- Against mchess2 (`make match-mchess2`, 20 games at 40 moves in 20
  seconds) it won 20 of 20. That is less than it seems: mchess2 at
  `65e8855` stops its iterative deepening at level 2
  (`while (level <= 2)` in `ai.cc`), whatever its time.
- In niceemu, `tools/match.py` played the NICE build, on its default
  `go`, against the host build at 5000 nodes a move, one game with each
  color from 1. e4 e5. The NICE build won both by checkmate, with no
  illegal moves.
- A full game in niceemu, in text: the NICE build played itself with
  `go` on every move, and black mated on ply 150. The 150 searches took
  39.5·10⁹ steps, 263 million a move on average (3.8 seconds on the
  board), and none more than 298 million. The last completed depth was
  5 on 102 moves, 4 on 31, 6 on 14, 3 once, and 1 twice (the search
  stops at depth 1 when there is one legal move or a mate in one).
- Size, from `make size`: code, runtime, and libc are 18894 words,
  4606 more than after Phase 1, so the program needs 35426 words, 6754
  over the budget. The largest new parts are `engine_search` (837
  words), the parsing of `go` (768), `search` (705), `strtoul` (510),
  and the printing of the search's info (412). In the linear memory,
  the zero-initialized data is 43.4 KB, of which the transposition table
  is 16 KB, and the peak of the wasm stack is 2.7 KB, so about 12 KB
  of the page is free. Getting the code under the budget is Phase 4's.

**Phase 3** is done (2026-11-02):

- `src/ui/screen.c` is the windowed front end, `build/nice/chess_screen.bin`.
  The board is 8×8 squares of 5×3 cells, with the ranks in column 0 and
  the files in row 24. Each piece is its letter on a chip of three
  cells, black on white or white on black, which reads well on both
  colors of square. The last move, the piece picked up, the squares it
  can go to, and a king in check have colors of their own, and the
  cursor is a pair of red brackets. The right half has the players,
  the state of the game, the level, the engine's depth, score, nodes,
  and principal variation, the last 18 moves in two columns, the move
  being typed (with the hardware cursor), a line for errors, and the keys.
- A move is made with the arrows and Enter (or Space), or typed as
  `e2e4` and Enter; an under-promotion is typed, and a promotion by
  the cursor is to a queen. Esc drops the piece or the typed text, and
  during a search makes the engine move now. U takes back the player's
  last move and the reply, S switches sides, R rotates the board, N
  starts a new game, + and − set the level (1 to 5: 1/9, 1/3, 1, 3,
  and 9 times `platform_move_time`; 3 by default), and Q quits. The
  letters of commands are not files or promotion pieces in the place
  they are typed, so the two kinds of keys do not clash.
- The engine polls the keyboard during its search through
  `platform_pollc`, new in `src/platform/`, but only once a read has
  found no byte waiting, that is, once the input is a keyboard. From a
  file (`niceemu -i`) a read waits instead, and the program then reads
  the file only between moves, so a run of the tests is the same every
  time. Keys pressed during a search, other than Esc, wait for after it.
- Each move also goes to the console as `move e2e4`, and the end of the
  game as `result ...`. `tools/screen_game.py` uses that to play white
  through the keys, with the host build choosing the moves, half of
  them typed and half by the cursor, and saves the keys.
- The front end does not use `printf`. `engine.h` has two new calls,
  `engine_move_from` and `engine_move_to`, for the colors of the squares.
- Tests: `tests/screen.keys` (the level, a move by the cursor, a typed
  move, Backspace, undo, switching sides, and an illegal move) runs in
  `make test`, and its screen with the colors from `niceemu --color` is
  compared. `make screen-game` (in CI, about a minute) plays the keys of a
  full game that `screen_game.py` recorded: 75 moves at level 1, until
  the engine mates, 2.3·10⁹ steps, compared the same way.
  `tools/screenshot.py` draws the screen as a PNG, from a dump of the
  screen's memory, in the font of the window.
- A full game in `niceemu --window`: the same keys, in the window build
  of niceemu V1.2 with SDL's dummy video driver and `--speed 0`, played
  the same 150 plies to the same checkmate. A game typed by hand in a
  real window has not been played yet, and nothing automated presses
  Esc during a search, as only a keyboard makes the search poll.
- `--window` runs one step per microsecond by default, so a move at
  level 3 (300 million steps) takes five minutes there. With
  `--speed 0` it takes about 7 seconds, as without the window.
- Not done, from the plan: clocks. In niceemu the clock counts steps,
  and in the window with `--speed 0` it would run about 40 times too
  fast while the player thinks, so the panel shows the level instead.
- Size, from `make size`: the code is 14624 words, against 18894 for the
  text front end, as `printf` is not linked. It needs 31224 words, 2552
  over the budget. The front end takes about 4200 of them (`main`, with
  the handling of the keys, 1494; `draw_info` 1358; `draw_all` 1323).

## Decisions

These were open questions in the first version of this plan. They were
decided on 2026-11-02.

1. **nice_cpu is pinned to a release**, V1.2 now, and the pin is raised by
   hand.
2. **No copy of mchess2 in this repository.** The archived mchess2
   repository is the reference, at commit `65e8855`.
3. **More RAM on the MEGA65 is requested from NiceMonMEGA65**, in
   [NiceMonMEGA65#68](https://github.com/MJoergen/NiceMonMEGA65/issues/68). Until it exists, 28K words is the target, and the
   size work of Phase 4 plans for it.
4. **Coordinate notation (`e2e4`) only, for now.** SAN (`Nf3`) is a
   nice-to-have, postponed until after Phase 4, because it costs code on
   the board.
