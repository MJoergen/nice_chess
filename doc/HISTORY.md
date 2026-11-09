# History

How nice_chess came to be, after the fact. What is true now is in the
README and `src/README.md`; open items are
[GitHub issues](https://github.com/MJoergen/nice_chess/issues).
Investigations made along the way are in [LOG.md](../LOG.md).

## The plan

nice_chess is a port of [mchess2](https://github.com/MJoergen/mchess2),
a C++ chess engine for a PC (about 4300 lines, 2017), to the NICE CPU of
[nice_cpu](https://github.com/MJoergen/nice_cpu): first to its emulator
niceemu, then to the MEGA65 under the monitor of
[NiceMonMEGA65](https://github.com/MJoergen/NiceMonMEGA65). It was
planned in commit `b9627d1` (the plan is in git as
`doc/PORTING_PLAN.md`) and done in five phases, all on 2026-11-02.

The NICE toolchain compiles C through clang and wasm32 and has no C++
standard library, so the engine was rewritten in C. It started from
`runtime/libc/test/mchess.c` of nice_cpu V1.2, a one-file C version of
an earlier mchess by the same author, and took castling, en passant,
FEN, and the Zobrist keys from mchess2. It was split into a core
without I/O, which builds for NICE and the host alike, and two front
ends, one in text and one on the 80×25 screen.

At the start the MEGA65 gave a program 28K words, and `mchess.c` needed
about 10¹⁰ steps for one move at depth 5, so the plan put memory first
and speed second: one global move stack, `int32_t` everywhere, 32-bit
Zobrist keys, a small transposition table, and an evaluation kept up to
date in make and undo.

## Decisions

1. nice_cpu is pinned to a release, and the pin is raised by hand: V1.2
   at first, V1.6 for the font of #12. From V1.3 niceemu's microsecond
   counter counts the host's time, so the tests and tools run it with
   `--step-time`, where it counts steps as before.
2. No copy of mchess2 in this repository. mchess2 is archived, and the
   test suites are fetched from it at commit `65e8855`.
3. More RAM on the MEGA65 was requested from NiceMonMEGA65
   ([NiceMonMEGA65#68](https://github.com/MJoergen/NiceMonMEGA65/issues/68)).
   Its release V1.2.1 gave the MEGA65 128K words, and a program
   under the monitor the words `0x8000` to `0x1EFFF`, 92K, before Phase 4,
   so the size work planned for 28K words was never needed.
4. Coordinate notation (`e2e4`) only. SAN was postponed, as it costs
   code on the board (#5).

## Phases

**Phase 0** (`26a6260`): the Makefile, which fetches nice_cpu and niceemu
at V1.2, and CI. `mchess.c`, unchanged, ran `perft 3` in niceemu.

**Phase 1** (`08c34ca`): the core in `src/engine/`, with castling, en
passant, under-promotion, FEN, 32-bit Zobrist keys, and the game result.
The perft suite, 126 positions, matched on the host to depth 5 and in
niceemu to depth 3. The program was 30860 words, 2188 over the 28K, of
which `printf` was 3727. The quiescence search of `mchess.c`, without
move ordering, took more than 2·10⁹ steps for `go 1` on Kiwipete, and
its mobility evaluation 79% of the steps of a search (LOG.md).

**Phase 2** (`4045538`): MVV-LVA ordering and an evaluation by material
and piece-square tables first, then alpha-beta by iterative deepening
with quiescence, a check extension, a transposition table of 2048
entries, killer moves, and draws inside the tree; the text front end,
and UCI. `go depth 5` from the opening went from 10¹⁰ steps to 46
million. The host build beat random moves 20 of 20 and mchess2 20 of 20
(mchess2 at `65e8855` searches only to level 2); in niceemu the NICE
build played itself to mate in 150 plies.

**Phase 3** (`709717a`): the windowed front end, `src/ui/screen.c`,
with moves by the cursor or typed, the search's info, a move list, and
Esc to make the engine move now. `make screen-game` replays a full game
of 75 moves by its keys in CI. Without `printf` it was 31224 words.

**Phase 4** (`a2b6eb3`): the MEGA65 R6, with NiceMonMEGA65 V1.2.1.
Both front ends fit the 92K words with about 59K to spare, `printf`
included, so they were built as they were, at `0x8000` for the
monitor. The engine runs about 54 million steps a second there (1.85
cycles per instruction, not the 1.43 of nice_cpu's benchmark), so a move
takes 5 seconds and reaches depth 5 from the opening. The tests pass on
the board over Ethernet and the debug UART. The engine, black at level
3, mated the host build in 56 moves in a game played by its keys over
the link ([the screenshot](screenshot.png)), and in UCI on the board it
scored 1.5 of 2 against the host build at 5000 nodes a move.

What was left over became issues #2 to #12.

## After the phases

The work from 2026-11-03 to the first release, by subject. The
measurements are in LOG.md, by date.

**The rating** (`e227034`, `ec3089a`, 2026-11-03 and 2026-11-04):
`make rate` plays the host build without asserts at the board's number
of nodes a move against Stockfish's UCI_Elo levels, with cutechess-cli
and Ordo, so that the engine is rated as it plays on the MEGA65,
without the board; `tools/board_uci.py` makes the board itself a UCI
engine of the host. At 83000 nodes a move, the board's 5 seconds then,
600 games gave 1817 ± 30 Elo on the CCRL 40/4 scale, about 1800. On
2026-11-09, 2000 games rated each of the 5 levels (`LEVELS`), as the
seeds of one gauntlet at their shares of the nodes: 1607 to 2054 Elo,
1847 at level 3.

**The search** (`641a6e1` to `8f724d8`, 2026-11-04): the NICE builds
went to `-O 2` (#7), and the transposition table to 16384 entries,
128 KB in a third page of linear memory, once the 92K words allowed it
(#6); then a principal variation search (#4), null-move pruning, a
history table with late move reductions, `for_side` in place of the
products by the side, and lists of each side's pieces in place of a
scan of the board. The text front end hears `stop` during a search
(#9). Each change was measured in niceemu steps, and the search changes
in 2000 games against the build before: null move +71 Elo, history and
LMR +51, PVS +16. Last, the NICE builds dropped wasm's bounds checks,
with the host build's asserts and `CHECK_STATE` as the check in their
place: about 4% fewer steps and 3800 words fewer. From the opening, the
default time of a move reaches depth 7, where Phase 4 reached depth 5;
the board searches about 93000 nodes in its 5 seconds, against 83000.
On 2026-11-05 the board won both games of `make board-match` against
the host build at 5000 nodes a move, over the debug UART.

**The opening book** (`3839229` to `5ba0138`, 2026-11-04): 122 lines
in `src/engine/book.txt`, each with Stockfish's score at its end, make
a table of 403 positions by the engine's own Zobrist keys, which
`tools/mkbook.c` builds on the host. The windowed front end plays from
it at first, and O turns it off; the text front end has `book on`, and
UCI the option `OwnBook`.

**The front ends** (2026-11-04 to 2026-11-08): a clock for each side
in the window (#8, `12ea6d2`), which in niceemu counts steps as the
board's time; the pieces as glyphs in the font RAM (#12, `55970b7`),
which took nice_cpu V1.6, for the font in its emulator's window, and
NiceMonMEGA65 V1.6, for the font RAM on the board; and Q leaves the
screen in the monitor's colors (`eede674`). `make board-stockfish`
plays the board's screen against Stockfish through its keys
(`cf0becb`).

**The release** (`cf94e8b`, 2026-11-06): doc/RELEASE.md, the steps of
a release, and `tools/release.py`, which scripts them: the preflight,
the check on the board and its stamp, the wait for CI, the tag with the
section of RELEASE_NOTES.md as its message, and the workflow that
builds the archive and the GitHub release. The archive has the MEGA65
builds under 8.3 names, as NiceMonMEGA65 V1.9 loads them from the SD
card (`8b4af80`).
