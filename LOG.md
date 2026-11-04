# Log

Investigations made during the development (doc/HISTORY.md), newest first.

## 2026-11-04: Null-move pruning

Before the moves of a node, the side to move passes, and the other side
searches the position with a null window around beta, two plies
shallower than the moves would be (`NULL_R`). If that is still at
least beta, the node is cut. It is not tried in check, right after a
null move, at depth 1, with a full window (the principal variation),
when the evaluation is below beta, or when the side to move has only
its king and pawns, where zugzwang is common. A cut by a mate score
returns beta, as a mate after a pass is not a proven one.

Steps in niceemu V1.2, the text front end at `-O 2`:

| Run | PVS | Null move |
|---|---|---|
| 24 Bratko-Kopec positions, `go depth 4` | 1457.4M | 1091.6M (−25%) |
| 24 Bratko-Kopec positions, `go depth 5` | 5914.4M | 3518.2M (−41%) |
| `go` from the opening, to depth 6 | 211M, 65942 nodes | 48M, 13233 nodes |

From the opening, the default time of a move (300M steps) now reaches
depth 7. The steps a node stay the same, 2682 and 2690 on the
Bratko-Kopec positions at depth 4. The 300 WAC positions on the host
take 61% fewer nodes at `go depth 5` (26.95M to 10.61M) and 77% fewer
at `go depth 6` (126.1M to 28.9M), with the same move in 282 of them
at either depth. At 93000 nodes a position the search suite solves 317
of 548, against 308 (WAC 229 against 223), but the zugzwang positions
1 of 5, against 2. At 10000 nodes it solves 242, against 244. The
MEGA65 builds need 76010 and 74567 words, 442 and 455 more.

Games of the host build against the build before, both at 93000 nodes
a move, from 8moves_v3.pgn, each opening with both colors,
cutechess-cli 1.5.1:

| Games | Null move wins, losses, draws | Score | Elo |
|---|---|---|---|
| 2000 | 963, 560, 477 | 60.1% | +71 ± 14 (LOS 100%) |

The game of `tests/screen_game.keys`, recorded again with
`tools/screen_game.py`'s defaults, is now won by the engine at level 1,
against the host build at 2000 nodes a move.

## 2026-11-04: Principal variation search

Issue #4. Each node searches its first move with the full window, and
the others with a null window, again with the full window when one
turns out better; the root does the same. Steps in niceemu V1.2, the
text front end at `-O 2`:

| Run | Alpha-beta | PVS |
|---|---|---|
| 24 Bratko-Kopec positions, `go depth 4` | 1872.5M | 1457.4M (−22%) |
| `go` from the opening, to depth 6 | 264M, 77961 nodes | 212M, 65942 nodes (−20%) |
| `tests/search.input` | 192.7M | 187.2M (−3%) |

The steps a node stay the same, 2668 and 2682 on the Bratko-Kopec
positions. The 300 WAC positions at `go depth 5`, on the host, take
19.8% fewer nodes (33.59M to 26.95M), with fewer in 263 of them. In
all 324 positions the move and the score are the same. The fixed
searches of `tests/search.input` are shallow, so there the
re-searches cost about what the null windows save. At 10000 nodes a
position, the search suite solves 244 of 548, against 237. The MEGA65
builds need 75239 and 74112 words, 65 and 67 more.

Games of the host build against the build before, both at 93000
nodes a move (the board's 5 seconds), from 8moves_v3.pgn, each opening
with both colors, cutechess-cli 1.5.1:

| Games | PVS wins, losses, draws | Score | Elo |
|---|---|---|---|
| 400 | 165, 150, 85 | 51.9% | +13 ± 30 |
| 2000 | 806, 713, 481 | 52.3% | +16 ± 13 (LOS 99.1%) |

At equal nodes the gain is from the depth that the nodes saved buy.
The game of `tests/screen_game.keys`, recorded again with
`tools/screen_game.py`'s defaults, is now won by white, the host build
at 2000 nodes a move, where the engine at level 1 won before: one game,
against the matches above.

## 2026-11-04: A transposition table of 16384 entries, and the board's nodes a move

Issue #6. Steps in niceemu V1.2, the text front end at `-O 2`, by
`TT_BITS`: the 24 Bratko-Kopec positions, each from a cleared table,
at the default time of a move, 300M steps. The time to a depth is
summed over the positions that reach it with every size.

| `TT_BITS` | Entries | To depth 4 (23 positions) | To depth 5 (9) | Mean depth |
|---|---|---|---|---|
| 11 | 2048 | 100% | 100% | 4.46 |
| 12 | 4096 | 96.8% | 85.8% | 4.54 |
| 13 | 8192 | 92.8% | 76.1% | 4.67 |
| 14 | 16384 | 91.0% | 74.7% | 4.67 |
| 15 | 32768 | 89.7% | 71.8% | 4.71 |

A game of the engine against itself, 30 moves at the default time,
was also played with each size, but the games part at the second or
third move, so their figures do not compare.

15 (256 KB) does not fit the 92K words with the code. 14 (128 KB) needs
three pages of linear memory: the MEGA65 builds then need 75174 and
74045 words, with 19002 and 20131 to spare. The board's loader clears
the memory before it writes the program, and `tt_clear` clears the
table on each new position, so the larger table, all zeros, adds
nothing to the `.bin` file.

With `-O 2` and this table, a move from the opening at the default
time completes depth 6 (77961 nodes, 264M steps), where it completed
depth 5 before. The fixed searches of `tests/search.input` find the
same moves and scores with up to 27% fewer nodes. At 10000 nodes a
position, the search suite solves 237 of 548, against 239.

The nodes of 5 seconds, measured on the board this time: every 11th
position of mchess2's search suite, 50, each with `go movetime 5000`
on the MEGA65 R6 (NiceMonMEGA65 V1.2.1, over Ethernet). From the last
info line of each, where the search had run more than 370 ms (48):

| | Nodes a second |
|---|---|
| min | 11729 |
| 25% | 17329 |
| median | 18559 |
| 75% | 20947 |
| max | 24323 |

So 5 seconds a move is about 93000 nodes, against 83000 before
(`make rate`'s `NODES`). The same positions in niceemu, with `go
movetime 270000`, take 2723 steps a node at the median (3245 before)
and end at the same depth in 49 of 50. Paired by position, the board
runs 51.9M steps a second of the `-O 2` program (50.5M to 53.2M for the
middle half), a little under the 54M of the `-O s` program.

## 2026-11-04: -O 2

Issue #7. Steps in niceemu V1.2, the text front end, with the
transposition table of 2048 entries:

| Run | `-O s` | `-O 2` |
|---|---|---|
| `tests/search.input` | 232.3M | 197.1M (−15%) |
| `go 5` from the opening | 45.7M | 38.2M (−16%) |
| Kiwipete `perft 3` | 91.3M | 73.2M (−20%) |
| Code, text front end, words | 20154 | 25805 |
| Code, windowed front end, words | 16477 | 24495 |

The code grows by 5651 and 8018 words, more than the 2900 measured
before Phase 2. The MEGA65 builds then need 42406 and 41239 words,
with 51770 and 52937 to spare.

## 2026-11-04: A rating at the board's speed

`make rate` with its defaults and `CONCURRENCY=8`: `build/host/chess_fast`
at 83000 nodes a move, the board's 5 seconds, against Stockfish 16 at
UCI_Elo 1700, 1900, and 2100, at 60+0.6. 200 games each, from
8moves_v3.pgn (`make book`), each opening with both colors.
cutechess-cli 1.5.1, Ordo 1.2.6. It took 107 minutes.

| Opponent | Score |
|---|---|
| sf1700 | 53% |
| sf1900 | 37% |
| sf2100 | 35% |
| All, 600 games | 42% |

Ordo, with each Stockfish anchored at its UCI_Elo: 1817 ± 30, on the
CCRL 40/4 scale. 3% of the games were draws. There were no losses on
time and no illegal moves.

The ± 30 is only the error of the number of games. The engine scored
nearly the same against 1900 and 2100, so Stockfish's UCI_Elo levels
are not 200 apart against this engine; the rating is about 1800 within
50 to 100.

On the board, 4 games of `tools/board_uci.py` against Stockfish at
UCI_Elo 1800, at 40/200 for the board, gave 3 of 4: too few to
measure, and not against the rating.

## 2026-11-03: The board's nodes a move, and a first rating

To rate the engine as it plays on the MEGA65, the host build plays at
the board's number of nodes a move.

The nodes: 48 positions of mchess2's search suite, every 11th, each
with `go movetime 270000` in niceemu V1.2 (270 million
steps, 5 seconds on the board). From the last info line of each, where
the search had run more than 20 million steps, a node takes:

| | Steps a node |
|---|---|
| min | 2714 |
| 25% | 2911 |
| median | 3245 |
| 75% | 3498 |
| max | 5591 |

So the board searches about 16600 nodes a second, and 5 seconds a move
is about 83000 nodes. The opening is slower, 2700 steps a node at depth
5 and 4000 at depth 6 (the entry of 2026-11-02), which gives 13000
nodes a second there.

The same moves: 8 of these positions with `go nodes 83000` gave the
same info lines and the same best move in niceemu and on the host, both
with `build/host/chess_text` and with `build/host/chess_fast` (without
asserts and `CHECK_STATE`). The host takes about 25 ms for it.

A first rating, before cutechess-cli was at hand: `tools/match.py`,
`build/host/chess_fast` at `go nodes 83000` against Stockfish 16
(Ubuntu's package) with `UCI_LimitStrength` and one thread, at 40 moves
in 40 seconds, 20 games each from match.py's 10 openings:

| Stockfish UCI_Elo | nice_chess's score |
|---|---|
| 1320 | 19 of 20 |
| 1600 | 15 of 20 |
| 1900 | 8.5 of 20 |

The rating that fits all three best is 1824, with 1714 to 1939 at 95%
(likelihood ratio). Stockfish's UCI_Elo is fitted at 60+0.6 to CCRL
40/4, and here it had less time, and the openings were few, so this is
a range to aim the real gauntlet at, not a rating. All but one game
ended by checkmate. `make rate` plays at 1700, 1900, and 2100 by
default because of this.

## 2026-11-02: The board's speed on the engine

Phase 4's first runs on the MEGA65 R6 (NiceMonMEGA65 V1.2.1,
100 MHz, the pipelined core), with the text front end under the
monitor, against the same searches in niceemu V1.2. The `time` of the
info lines is real milliseconds on the board, and steps in niceemu:

| Search, from the opening | Nodes | niceemu, steps | MEGA65 | Steps a second |
|---|---|---|---|---|
| `go depth 5` | 16782 | 45.5M | 0.84 s | 54M |
| `go depth 6` | 99353 | 399.5M | 7.48 s | 53M |
| `go depth 7` | 792753 | — | 46.0 s | — |

So the engine runs about 54 million steps a second, 1.85 cycles per
instruction, where nice_cpu's benchmark gives 1.43 and the plan
assumed 70 million. The engine's code is loads, compares, and branches
over the board, with few of the long runs of arithmetic that the
benchmark has, which is where the pipeline's stalls would come from;
this was not measured further. For the plan: a step budget from
niceemu divides by 54 million, not 70, to give seconds on the board,
and the default of 300 million steps a move is 5.6 seconds there.
`platform_move_time()` gives the board 5 seconds.

## 2026-11-02: Speed and size, and what the system could do for them

The question: could routines in the monitor's ROM, or new hardware
devices, make nice_chess faster or smaller, if they are of general value
and not specific to chess?

The short answer: not for nice_chess. The size gap closes inside the
program, and the time goes to the engine's own design and to wasm's
memory model, which no device reaches. Of the general ideas, a
translator option to trust alignment helps every C program, and a
byte-address window in the memory system is worth measuring
([nice_cpu#112](https://github.com/MJoergen/nice_cpu/issues/112)).

All figures are steps in niceemu V1.2, from the commit after Phase 1
(08c34ca), built as `make` builds it (`-O s -D NDEBUG`), unless stated.
Steps are not cycles on the pipelined core.

### Where the steps go

A trace (`niceemu -t`) counted per address, and mapped to functions with
the `.sym` file:

| Function | `go 3` from the opening, 52.8M steps | Kiwipete `perft 3`, 81.2M steps |
|---|---|---|
| `gen_pseudo` | 79% | 11% |
| `is_attacked` | 11% | 48% |
| `make_move` and `undo_move` | 2% | 29% |
| `add_pawn_moves` | 5% | 0.5% |
| everything else | 3% | 11% |

- The search spends its time generating moves, because the evaluation
  counts mobility by generating all the moves of both sides at every
  leaf (mchess.c's evaluation, unchanged in Phase 1).
- Perft spends its time in `is_attacked`, because a move's legality is
  tested by making it and asking whether the king is attacked.
- Both are short ray scans over the 10×12 board, a few squares each.

Counted by kind of instruction, through `nicedis.py`, the memory model
of wasm takes 18% of the steps of the search and 24% of perft: the
compare and the branch of the bounds check of each 4-byte access, and
the shifts between byte addresses and word indexes. With
`--no-bounds-checks`, a test of the alignment (`AND[M] R0, a, 3` and a
branch) takes the place of the bounds check, and the steps hardly change.

### What the translator's options give

| Build | Program end, words | `go 3` | Kiwipete `perft 3` |
|---|---|---|---|
| `-O s` (now) | 30672 | 52.8M | 81.2M |
| `-O s --no-bounds-checks` | 28712 | 52.1M | 81.6M |
| `-O s --no-bounds-checks --no-trap-report` | 28435 | 52.1M | 81.6M |
| `-O 2` | 33528 | 44.3M | 65.9M |
| `-O 2 --no-bounds-checks` | 31086 | 42.6M | 60.2M |

The program end includes the linear memory (16384 words); the budget of
28672 words also covers the NICE stack (188 words), so 30860 words are
needed now, 2188 too many.

### Routines in the monitor's ROM

The monitor's README rules them out: an entry is added "only when a
program cannot do without it", and a C program's strings are bytes
packed four to a word in linear memory, where the ROM's routines cannot
reach them, and libc must run in niceemu, which has no ROM. Both could be
worked around, with the memory's base passed beside each byte address
and an option for niceemu to load a ROM image. The ROM has room: the
monitor and the demo take about 7.3K of its 32K words.

It is not needed here. `printf` costs 3727 words, and a small print
routine in the program recovers nearly all of them, which alone closes
the gap; `--no-bounds-checks` with `--no-trap-report` saves another 2237.
A C library in ROM would also tie each program to a version of the
monitor. Worth revisiting only if NiceMonMEGA65#68 (more RAM for
programs) comes to nothing and several C programs hit the 28K limit.

### Hardware devices

A call to a memory-mapped device costs 4 to 6 instructions to write its
operands and read its result, so it pays only when one call replaces
much more work than that, as the math unit does for a division. A ray
of one to three squares does not. The candidates, by general value:

1. **A bit-manipulation unit**, in the math unit's style: population
   count, leading and trailing zeros, bit reverse, byte swap, bit
   deposit and extract. Of use to bit sets, allocators, software floats,
   and compression. It would help chess only with bitboards, which suit
   a 32-bit processor poorly, as a bitboard is 64 bits.
2. **A copy and fill engine** (`memcpy`, `memset`): for scrolling the
   monitor's screen, clearing the memory at the start of a program, and
   the strings of a BASIC. Next to nothing for chess.
3. **CRC32**: for loading from the SD card or over Ethernet. Chess hashes
   positions with Zobrist keys, a table and XOR, and has no buffer to
   check.
4. **A source of entropy**: the one thing that software cannot do for
   itself. Chess would use it only to vary its openings.

### The memory model, and a prototype

The overhead of the memory model is general: every C, Zig, and Rust
program on NICE pays it. Two ways to cut it:

- **Trust the alignment hint.** A prototype of `--trust-alignment` for
  nicewasm and nicebuild.py (branch `trust-alignment` of a local
  nice_cpu, in the worktree `../nice_cpu-trust-alignment`, from
  `dev-V1.3`, not committed): an access of 4 or 8 bytes whose hint says 4
  bytes or more is taken to be at a multiple of 4, with no test and no
  slow path. A misaligned one then reaches the word of its first byte,
  which wasm does not allow, so it is an option like
  `--no-bounds-checks`. With bounds checks it saves nothing, as #73 of
  nice_cpu says, since one compare covers the bounds and the alignment:
  the steps are the same.

  | Build | Program end, words | `go 3` | Kiwipete `perft 3` |
  |---|---|---|---|
  | `-O s --no-bounds-checks` | 28712 | 52.1M | 81.6M |
  | `-O s --no-bounds-checks --trust-alignment` | 26288 | 48.5M (−7%) | 72.1M (−12%) |
  | `-O 2 --no-bounds-checks` | 31086 | 42.6M | 60.2M |
  | `-O 2 --no-bounds-checks --trust-alignment` | 28209 | 40.4M (−5%) | 54.8M (−9%) |

  Both trusting builds pass `tests/text` and the perft suite to depth 3
  in niceemu, and the prototype passes nice_cpu's `make test` and
  `make typecheck` in `translator/`. The gain is less than the 10% first
  guessed for the search. With it, `-O s` fits the budget with 2200
  words to spare, before `printf` is replaced.

- **Take byte addresses in hardware.** What is left of the conversion
  is one `SHR` per access, 3.2% of the steps of the search and 7.5% of
  perft. An operand `@c[Rx>>2]` would fold it into the address, but the
  operand modes are all taken, so it would need the C bit of the ALU
  instructions that ignore it, or a bit of SR, of which none is free. A
  byte-addressed window in the memory system needs no change of the ISA,
  and a second window for single bytes would make a byte load one
  `MOVE` instead of four instructions. Filed as an idea:
  [nice_cpu#112](https://github.com/MJoergen/nice_cpu/issues/112).

### What it means for the plan

- Speed: Phase 2's piece-square evaluation removes most of the 79% in
  `gen_pseudo`, more than any of the above. A generator that knows
  checks and pins would do the same for `is_attacked` in perft.
- Size: replacing `printf` (Phase 4) closes the gap by itself.
  `--no-bounds-checks` and `--no-trap-report` are free in speed and give
  2237 words more; `--trust-alignment`, if nice_cpu takes it, another
  2400, and 7–12% of the steps.
- `-O 2` is 16–19% faster than `-O s` for 2900 words more, a trade to
  make once the size fits.
