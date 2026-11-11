# Log

Investigations made during the development (doc/HISTORY.md), newest first.

## 2026-11-11: Where the engine loses, against Stockfish 2000

cutechess-cli played build/host/chess_fast as on the board, at 93000
nodes a move with OwnBook on, against Stockfish 16 at UCI_Elo 2000 and
60+0.6, 100 games from the start position, each engine white in half,
without adjudication (games/nice-vs-sf2000-20261111-070937.pgn). The
engine scored 53 wins, 7 draws, and 40 losses: 56.5%, +45 ± 67 Elo.

`make analyze` (tools/analyze.py) had Stockfish at full strength score
each of the 10783 positions for 1 second, 18 at a time, in about 10
minutes. A move's loss is the drop in the mover's expected score, by
lichess's curve: a blunder from 0.30. The opening is moves 1 to 12;
the endgame, the positions with a phase of 8 or less after it.

The moves out of the book, per 100 moves, with the mean centipawns
lost:

| Phase      | Side      | Moves | cp lost | Mistakes | Blunders |
|------------|-----------|------:|--------:|---------:|---------:|
| opening    | engine    | 990   | 24.8    | 0.7      | 0.2      |
| opening    | Stockfish | 1200  | 22.9    | 0.3      | 0.2      |
| middlegame | engine    | 2472  | 33.9    | 0.9      | 0.5      |
| middlegame | Stockfish | 2465  | 38.9    | 1.0      | 0.4      |
| endgame    | engine    | 1885  | 17.0    | 0.4      | 0.1      |
| endgame    | Stockfish | 1885  | 17.7    | 0.4      | 0.3      |

The middle game decides the games. Of the 40 losses, 32 fell below an
expected score of 0.2 for good in the middle game, 7 in the endgame,
and 1 in the opening; of the expected score given away in them, 59%
was in the middle game, 22% in the opening, and 20% in the endgame.
Most losses are a slide over several moves, not one blunder: there
were 17 blunders in the 100 games, 13 of them in the middle game. The
engine searches least deep there, a median of 6 plies, against 7 in
the opening and 8 in the endgame.

The engine saw none of its 17 blunders: after each, its own score was
1.5 pawns or more above Stockfish's. Stockfish's reply was a capture
in 5. The others were quiet moves, most of them an attack on the
engine's king: g4-g5 against the knight that guarded the castled king,
with the queen on h6 (game 96, where the engine thought itself 6.4
pawns ahead); Ng5xf7 (game 72); Qe4 against the king in the centre
after ...Qxc3 (game 50). The evaluation has no term for king safety,
and the attacks are deeper than the search.

The endgame is not a weakness against this opponent. 76 games reached
one: of those that began winning for the engine (expected score 0.75
or more) it scored 97% (36 games), of the balanced ones 58% (12), of
the losing ones 7% (28). Its endgame moves lost as much as
Stockfish's.

The book is not skewed, but short: the engine left it after a median
of 2 moves, and 13 games had no book move. Where it left the book,
Stockfish's score was a median of +0.14 for the engine and never below
-0.45 (as black, in the Sicilian, King's Indian, Slav, and Catalan).
After move 12 the engine's expected score was a median of 0.51.

## 2026-11-11: A bonus for passed pawns (#3)

A pawn with no pawn of the other side ahead of it on its own file or
the files beside it, and none of its own ahead of it on its file, got
a bonus by its rank, in the middle game and the end game: Stockfish's
classical PassedRank, scaled to PeSTO's pawn (82 and 94), 6, 11, 10,
40, 109, and 180 in the middle game and 13, 15, 19, 33, 80, and 117 in
the end game, from the second rank to the seventh. The bonus was not
kept: PeSTO's tables stay as they are.

The code: make and undo kept a Zobrist key of the pawns alone, and
the files of each side's pawns on each rank, as bits; the bonus was
kept in a table of 256 entries by that key, of 8 bytes each. A table
of 1024 entries did not fit the windowed front end's linear memory of
196608 bytes. The bonus agreed with its definition, square by square,
on 188331 positions from the search suite and random moves after them.

Its cost, by `make profile` (the 24 Bratko-Kopec positions at 3000
nodes): 169.8M steps against 161.0M, 5.5% more a node. evaluate took
85 steps a call against 28, make_move 280 against 242, undo_move 202
against 168. The table missed 16% of the calls, and 12% to 17% at
93000 nodes: captures and pushes in the search make new pawn
structures all the time. A miss first cost about 1400 steps, with the
pawns found in the piece lists; with the pawns kept by rank in make
and undo, about 200.

cutechess-cli played build/host/chess_fast with the bonus against
itself without, 2000 games a pairing at 93000 nodes a move, from the
openings of 8moves_v3.pgn, each with both colors, with cutechess-cli's
adjudication of make rate (games/*-vs-base-20261111-*.pgn):

| Bonus                  | Score          | Elo           |
|------------------------|----------------|---------------|
| 150%                   | 608-954-438    | -60.7 ± 13.6  |
| 100%                   | 683-829-488    | -25.4 ± 13.3  |
| 50%                    | 762-720-518    | +7.3 ± 13.1   |
| 25%                    | 667-717-616    | -8.7 ± 12.7   |
| End game only, 100%    | 731-739-530    | -1.4 ± 13.0   |
| End game only, 50%     | 739-711-550    | +4.9 ± 13.0   |

A small bonus gains nothing that 2000 games show, and a large one
loses. PeSTO's pawn tables were tuned on game results, so they already
pay what a pawn on a rank is worth on average, and a passed pawn is
mostly an advanced one; the bonus counts it twice. The matches give
both sides the same nodes, so the 5.5% more steps of the board would
cost about 5 Elo more.

## 2026-11-10: Pondering at the board's speed (#17)

The host's engine was held to the board's speed for this: a build of
build/host/chess_fast (with the PeSTO tables) whose platform_pollc,
which the search calls every 1024 nodes, sleeps until 1024 nodes at
18600 a second have passed. It searched 94208 nodes in 5 seconds,
against about 93000 on the board. The build was not committed. Every
game was at Blitz 3+2, from the openings of 8moves_v3.pgn, each with
both colors, under cutechess-cli; no game was lost on time.

Pondering against no pondering, 400 games: 169 wins, 111 losses, and
120 draws, +50.7 ± 28.7 Elo, as on the host at 10+0.1 (+59.6).

Each against Stockfish 1800 and 2200 (UCI_Elo, at 3+2 too), 100 games
a pairing:

| Engine    | SF 1800 | SF 2200 | Elo        |
|-----------|--------:|--------:|-----------:|
| Ponder    | 54%     | 44%     | 1988 ± 56  |
| No ponder | 66%     | 44%     | 2062 ± 55  |

Ordo, with the two Stockfish anchored. The difference, -75, is within
its error, about ±78: no gain from pondering showed against Stockfish.

The rate of hits explains most of it. In 6 games at 20+0.2 each, the
engine's expected reply was played 145 times of 180 against itself,
and 95 times of 298 against Stockfish 1800. So self-play overstates
pondering, and against Stockfish it is likely worth 15 to 20 Elo, too
little for 400 games to show. Against a player, its worth depends on
how often the engine guesses the reply.

## 2026-11-10: The PeSTO tables

The evaluation's material and piece-square tables were those of
Michniewski's "Simplified Evaluation Function", set by hand, with one
table for both phases but for the king, and a bonus by rank for
end-game pawns. PeSTO's (Ronald Friederich, from Rofchade) were
tuned on game results, for the same evaluation: material and tables,
in the middle game and the end game, blended by the same phase. They
were taken unchanged from chessprogramming.org.

cutechess-cli played build/host/chess_fast with PeSTO's tables against
itself with the old ones, 2000 games at 93000 nodes a move (5 seconds
on the board), from the openings of 8moves_v3.pgn, each with both
colors, 10 games at a time on 20 cores. PeSTO's scored 1236 wins, 460
losses, and 304 draws: +142.3 ± 15.0 Elo.

The search suite at 10000 nodes solved 272 of 548, against 247; most
of the gain is in sbd, the positional suite, 92 of 134 against 68.
The tables are int16_t now, as some entries are over 127: the text
front end is 22 words larger, the windowed one 7 words smaller.

The strengths, with the same table of noise and share, were rated
again as on 2026-11-10: `make rate` with `STRENGTHS="1 2 3 4 5"`,
139500 nodes a move, against Stockfish 1320, 1600, and 2000, Blunders
0.1.0, and Belofte 2.1.9, at 60+0.6, 100 games a pairing (2500 games),
10 at a time. Blunders crashed in 6 games, which count as losses for
it. The scores of the strengths:

| Strength | Belofte | Blunders | SF 1320 | SF 1600 | SF 2000 |
|---------:|--------:|---------:|--------:|--------:|--------:|
| 5        | 98%     | 93%      | 92%     | 79%     | 61%     |
| 4        | 89%     | 76%      | 81%     | 71%     | 50%     |
| 3        | 70%     | 53%      | 84%     | 46%     | 26%     |
| 2        | 23%     | 12%      | 44%     | 13%     | 8%      |
| 1        | 6%      | 3%       | 38%     | 10%     | 5%      |

Rated with Stockfish's anchors alone, as before:

| Strength | Elo  | Error | Step | Old tables | Gain |
|---------:|-----:|------:|-----:|-----------:|-----:|
| 5        | 2005 | 48    |      | 1852       | +152 |
| 4        | 1840 | 42    | 165  | 1729       | +111 |
| 3        | 1651 | 38    | 189  | 1576       | +75  |
| 2        | 1300 | 42    | 352  | 1280       | +20  |
| 1        | 1182 | 49    | 118  | 1136       | +46  |

Full strength gained about as much against Stockfish as against the
old tables. The gain shrinks with the strength, as the noise covers
more of the evaluation: 823 Elo from 5 to 1, against 717. Strength 5
now scores 61% against Stockfish 2000, the highest anchor, so its
rating is the least sure. The step from 3 to 2 is still the widest.

## 2026-11-10: Pondering in UCI (#17)

cutechess-cli played build/host/chess_fast with `ponder` against itself
without, 400 games at 10+0.1 from the openings of 8moves_v3.pgn, each
with both colors, 8 games at a time on 20 cores, so that each engine
had a core of its own. Pondering scored 168 wins, 100 losses, and 132
draws: +59.6 ± 28.1 Elo. No game was lost on time.

## 2026-11-10: The strengths against Stockfish (#19)

`make rate` with `STRENGTHS="1 2 3 4 5"` plays each strength at
139500 nodes a move, Blitz 3+2's first move on the board, against
Stockfish at UCI_Elo, at 60+0.6, 100 games a pairing.

With the table of 2026-11-09, against Stockfish 1400, 1600, 1800, and
2000 (2000 games), the strengths were 1814, 1783, 1692, 1646, and
1637 (±40): 177 Elo from 5 to 1, about a third of the 537 of the round
robin. Strengths 2 and 1 were about equal.

Strength 1 (1/3.8 of the nodes) with more noise, against Stockfish
1320 and 1600, 120 games each:

| Noise | Elo        |
|------:|-----------:|
| 150   | 1478 ±66   |
| 250   | 1436 ±65   |
| 400   | 1331 ±72   |
| 600   | 1024 ±116  |

The new table, ±90, ±160, ±360, and ±500 for strengths 4 to 1, aimed
at 150 Elo a step. Its run, 2500 games, against Stockfish 1320, 1600,
and 2000, and two engines of the CCRL 40/4 list below Stockfish's
lowest UCI_Elo, Blunders 0.1.0 (1266) and Belofte 2.1.9 (1153), at
60+0.6 too. The scores of the strengths:

| Strength | Belofte | Blunders | SF 1320 | SF 1600 | SF 2000 |
|---------:|--------:|---------:|--------:|--------:|--------:|
| 5        | 92%     | 80%      | 91%     | 69%     | 42%     |
| 4        | 88%     | 65%      | 78%     | 60%     | 32%     |
| 3        | 67%     | 38%      | 73%     | 44%     | 23%     |
| 2        | 15%     | 10%      | 49%     | 16%     | 8%      |
| 1        | 8%      | 5%       | 30%     | 7%      | 3%      |

The two sets of anchors disagree. With Stockfish's alone, Blunders
rates 1637 and Belofte 1479 (±44), about 350 above their CCRL ratings:
they play on their clocks, on a host much faster than CCRL's, while
the engine plays a fixed number of nodes, and Stockfish's UCI_Elo is
fitted at 60+0.6. So the ratings are those with Stockfish's anchors:

| Strength | Elo  | Error | Step |
|---------:|-----:|------:|-----:|
| 5        | 1852 | 41    |      |
| 4        | 1729 | 38    | 124  |
| 3        | 1576 | 38    | 153  |
| 2        | 1280 | 42    | 296  |
| 1        | 1136 | 53    | 144  |

717 Elo from 5 to 1. The step from 3 to 2 is twice the others; the
table was kept.

## 2026-11-09: The strengths below full (#19)

A strength below full adds noise to the evaluation, and searches a
share of the time or the nodes. The aim: about 500 Elo from strength 5
to strength 1, in even steps. Every game was on the host
(build/host/chess_fast), with cutechess-cli, at 140000 nodes a move for
full strength: about what Blitz 3+2 gives the engine on the board, 7.5
seconds at about 18600 nodes a second. The openings were those of
8moves_v3.pgn, each with both colors, and Ordo rated the players.

First, a fixed node cap and noise, each alone, against full strength,
60 games each (about ±90 Elo):

| Noise | Node cap | Elo |
|------:|---------:|----:|
| 20    |          | +6   |
| 40    |          | -18  |
| 80    |          | -209 |
| 150   |          | -185 |
|       | 60000    | -142 |
|       | 30000    | -355 |
|       | 15000    | -488 |
|       | 7000     | -715 |
|       | 3000     | -838 |

The nodes do most of it, about 200 Elo for each halving. A fixed cap
would then be a larger handicap at a slower time control, as full
strength gets more nodes there: about 700000 a move at Rapid, and 3
million at Classical. So a strength divides the budget, time or
nodes, instead, which costs about the same at any time control.

Pairs of noise and share against full strength, 100 games each (about
±70 Elo), gave -117 for 40 and 1/2, -243 for 60 and 1/3, -350 for 80
and 1/3, and -500 for 100 and 1/5. In a round robin of all five,
those steps came out wider, 705 Elo from 5 to 1. The milder table, in
a round robin of 2600 games, 1040 for each strength:

| Strength | Noise | Share | Elo  |
|---------:|------:|------:|-----:|
| 5        | 0     | 1     | 0    |
| 4        | 25    | 1/1.5 | -141 |
| 3        | 45    | 1/2.2 | -270 |
| 2        | 60    | 1/3   | -406 |
| 1        | 75    | 1/3.8 | -537 |

These are Elo among the engine's own strengths; against other players,
such as Stockfish in `make rate`, the steps are likely smaller.

## 2026-11-09: What the bytes of CHESS.BIN are

CHESS.BIN, the windowed front end built for the MEGA65 (nice_cpu V1.6,
`-O 2`), is 471870 bytes. It is the text `.bin` format of the
toolchain: a line `AAAAAAAA DDDDDDDD` of 18 bytes for each word that
is not 0. So the file is 26215 words, about 105 KB as 32-bit words.
The words that are 0, such as the 43988 free words of the linear
memory, are not in the file.

Each line was counted against the symbol before its address, from
`build/mega65/chess_screen.sym`. A static function is counted with the
exported function before it, which is in the same file.

| Part | Words | KB of the file | Share |
|---|---|---|---|
| Engine code | 13944 | 245 | 53% |
| - search.c | 4962 | 87 | 19% |
| - fen.c (FEN, and moves as text) | 2859 | 50 | 11% |
| - makemove.c | 1757 | 31 | 7% |
| - board.c | 1690 | 30 | 6% |
| - movegen.c | 1244 | 22 | 5% |
| - book.c | 586 | 10 | 2% |
| - tt.c, eval.c, game.c | 846 | 15 | 3% |
| Front end code (screen.c) | 7936 | 140 | 30% |
| Data segments | 3116 | 55 | 12% |
| - the glyphs of the pieces (pieces.h) | 1728 | 30 | 7% |
| - the table of the book (535 entries) | 1070 | 19 | 4% |
| - the tables of eval.c, strings, other constants | 318 | 6 | 1% |
| Runtime, libc, and nice.c | about 1240 | about 22 | 5% |

The code is 83% of the file. The translator makes several NICE
instructions of each wasm instruction: `main` of screen.c is 2528
words, and `draw_all` 2794. perft.c is not in the file, as the
windowed front end does not call it. CHESST.BIN, the text front end,
is 453312 bytes, with the same engine.

The size of the file does not limit the board; the image in words
does. `make size` gives 72251 words for this build, against the budget
of 94176. Only a binary format, in the monitor's loader, would make
the file much smaller.

## 2026-11-09: A rating of each level

`make rate`, now of each of the 5 levels (`LEVELS`), with its defaults
and `CONCURRENCY=8`: `build/host/chess_fast` at 1/9, 1/3, 1, 3, and 9
times 93000 nodes a move, the levels' shares of the board's 5 seconds,
against Stockfish 16 at UCI_Elo 1600, 1800, 2000, and 2200, at 60+0.6.
100 games for each level against each Stockfish, from 8moves_v3.pgn,
each opening with both colors: 2000 games, 400 a level, in one
gauntlet with the levels as its seeds. cutechess-cli 1.5.1, Ordo
1.2.6. It took 6 hours 14 minutes, from 2026-11-08 19:40.

| Level | Nodes a move | Depth | sf1600 | sf1800 | sf2000 | sf2200 | All | Ordo |
|---|---|---|---|---|---|---|---|---|
| 1 | 10333 | 4 | 36% | 25% | 18% | 12% | 23% | 1607 ± 44 |
| 2 | 31000 | 5 | 54% | 42% | 34% | 20% | 37% | 1769 ± 39 |
| 3 | 93000 | 6 | 66% | 44% | 44% | 25% | 45% | 1847 ± 40 |
| 4 | 279000 | 7 | 76% | 64% | 54% | 42% | 59% | 1980 ± 39 |
| 5 | 837000 | 8 | 82% | 73% | 60% | 46% | 66% | 2054 ± 40 |

Ordo rated the levels in one pool, with each Stockfish anchored at
its UCI_Elo, on the CCRL 40/4 scale. The depth is the median over the
moves of the games, from the comments of the PGN. 77 games, 3.9%,
were draws. There were no losses on time and no illegal moves. On the
host a move took 3 ms at level 1 and 200 ms at level 5.

Level 3 is 1847 ± 40, against 1817 ± 30 on 2026-11-04 at 83000 nodes,
before null move, history and LMR, PVS, and the table of 16384
entries: within the errors of both.

Each level has 3 times the nodes of the one below, and the steps
between them are 162, 78, 134, and 74 Elo: 112 on average, but more
uneven than the ± 40 of each explains. The Stockfish levels are not
200 apart against this engine: without anchors (cutechess-cli's own
table, with the pool at 0), sf1600, sf1800, sf2000, and sf2200 are at
−89, 3, 55, and 155, so 200 of UCI_Elo is about 80 Elo against these
levels, and the levels span 321 Elo there (−210 to 111), against 447
anchored. The anchoring puts each level on the UCI_Elo scale, as the
rating of 2026-11-04 was, but the distance between two levels is less
certain than their errors say. The levels are about 1600, 1770, 1850,
1980, and 2050, each within 50 to 100.

## 2026-11-04: The NICE builds without bounds checks

With `--no-bounds-checks` (nicewasm, through nicebuild.py), an access
to the linear memory has no compare and branch against its end; an
access out of bounds then reads or writes whatever NICE memory the
address reaches, where it trapped with code 2. `--trust-alignment`,
which would also drop the test of the alignment, is not in nice_cpu
V1.2. `--no-trap-report` was left out: it saves about 280 words and
no steps, and a trap of another kind still reports itself.

Steps in niceemu V1.2, the text front end at `-O 2`, against the
build before (piece lists). The nodes are the same:

| Run | Bounds checks | `--no-bounds-checks` |
|---|---|---|
| 24 Bratko-Kopec positions, `go nodes 3000` | 168.65M | 162.25M (−3.8%) |
| 24 Bratko-Kopec positions, `go depth 5` | 1985.0M | 1910.1M (−3.8%) |
| `go depth 6` from the opening | 30.02M | 28.66M (−4.5%) |
| Kiwipete `go depth 5` | 262.49M | 252.77M (−3.7%) |
| Kiwipete `perft 3` | 76.20M | 70.06M (−8.1%) |

The MEGA65 builds need 73520 and 72181 words, 3873 and 3829 fewer. As
a check that the engine makes no access out of bounds, the host build
with `-fsanitize=address,undefined` and `-DCHECK_STATE` ran the text,
search, and book tests, the perft suite to depth 4, the search suite
at 10000 nodes, and the 24 positions at depth 5, with no report.

With the three changes of today (`for_side`, the piece lists, and
this), the 24 positions at 3000 nodes take 16.0% fewer steps than
after the history table and LMR (193.16M to 162.25M, from about 2680
to 2250 steps a node), so the board searches about 19% more nodes
in the same time.

In `tests/screen.keys` level 1 now plays d2d4, which it finds within
depth 6 in its time, where it played e2e4 at depth 6.
`tests/screen_game.keys` is a new game, recorded again by
`tools/screen_game.py`; the engine at level 1 wins it.

`make board-match` on the MEGA65 R6 (2026-11-05, over the serial port,
`PORT=/dev/ttyUSB3`), the board at its 5 seconds a move against the
host build at 5000 nodes a move, from e2e4 e7e5: the board won both
games by checkmate, with black and with white. Two games against so
weak an opponent show that the build plays on the board at its clock,
not what the speed gives; `match.py` takes one board engine, so the
builds before and after cannot play each other on one board.

## 2026-11-04: Piece lists

`gen_moves` costs about 2870 steps a call, of which the scan of the 78
squares from a1 to h8 for the side's pieces is about 750. Counted on
the host, of its calls 72% (`go depth 6` from the opening) and 89%
(Kiwipete, `go depth 5`) are those of the quiescence search, for the
captures. A capture cuts 24% and 99.7% of the nodes that are cut; a
killer cuts most of the rest (72% from the opening). So generating
the captures and the quiet moves of a node in two stages would save
little: the quiescence search already generates only captures, and
it walks the same squares and rays as a full generation. A list of
the squares of each side's pieces saves the scan in every call.

`pos.pieces[2][16]` holds the squares, `pos.count` the places in use,
and `pos.index[120]` the place of the piece on each square. A first
version moved the last piece of a list into the place of a captured
one, which undo put back from a place saved in the history: 7
accesses to the lists in each of make and undo for a capture, and the
perft took 6% more steps than before. Each access to an array by a
computed index takes 6 to 8 instructions in the translated code (the
address, the shift to a word index, the bounds check). Now a captured
piece keeps its place, with square 0, which holds `IV`, so that
`gen_moves` passes over it in the `default` of its switch, with no
test of its own; a capture takes 3 accesses, and the count does not
change in a search. Each move also updates the place of the piece that
moves, in make and in undo.

Steps in niceemu V1.2, the text front end at `-O 2`, against the
build before (`for_side`). The 24 Bratko-Kopec positions at 3000
nodes each search the same number of nodes in both, which makes it
the measure of the steps a node:

| Run | Before | Piece lists |
|---|---|---|
| 24 Bratko-Kopec positions, `go nodes 3000` | 193.16M | 168.65M (−12.7%) |
| 24 Bratko-Kopec positions, `go depth 5` | 2149.1M | 1985.0M (−7.6%) |
| `go depth 6` from the opening | 53.76M, 13378 nodes | 30.02M, 8907 nodes |
| Kiwipete `go depth 5` | 297.67M, 128147 nodes | 262.49M, 123094 nodes |
| Kiwipete `perft 3` | 72.87M | 76.20M (+4.6%) |

Perft makes every move that it generates, so it pays the updates of
make and undo more often than the search does. The moves are generated
in the order of the lists, not of the squares, so the search visits
other nodes: the search suite at 10000 nodes solves 247 of 548, from
249 (sbd 68, from 70). In `tests/search.input` the mate in 2 of
`2rr3k/...` is found at depth 5, where it was found at depth 3: its
first move, the quiet g3g6, now comes late at depth 3 and is reduced
by LMR. The MEGA65 builds need 752 and 795 words more. A FEN with more
than 16 pieces of a side is now invalid.

## 2026-11-04: Where the steps go, and the multiplications by the side

A trace (`niceemu -t`) of the text front end at `-O 2`, with the steps
counted by function through the `.sym` file, after the history table
and LMR (01eec53):

| Function | `go depth 6` from the opening, 55.2M steps | Kiwipete `go depth 5`, 304.4M steps |
|---|---|---|
| `gen_moves` | 59.1% | 41.6% |
| `is_attacked` | 9.7% | 14.3% |
| `make_move` and `undo_move` | 8.2% | 18.4% |
| `score_moves` | 8.7% | 6.6% |
| `search` and `quiescence` | 10.1% | 14.2% |
| `W.cload4` and `W.load4` (the runtime's loads) | 2.2% | 2.7% |
| everything else | 2.0% | 2.2% |

The evaluation is now 0.5% to 1.1%; in Phase 1 the move generation
of its mobility took most of the steps. The ray loop of `gen_moves`
takes about 45 instructions a square: the bounds checks and the shifts
of wasm's memory model, locals kept on the stack, a boolean made and
tested again, and the products `target*s`, which the translator makes
on the math unit, as it does any product by a variable, and by -6 and
-10.

`for_side(p, side)` (internal.h) gives `p*side` by an XOR and a
subtraction, and takes the place of those products in `gen_moves`,
`has_pieces`, `is_attacked` (`e*WK`), and the square of an en passant
capture in `make_move` and `undo_move`. A product on the math unit
takes 3 instructions, two stores and a load, so each saves one:

| Run | Before | `for_side` |
|---|---|---|
| `go depth 6` from the opening | 55.22M | 53.76M (−2.6%) |
| Kiwipete `go depth 5` | 304.40M | 297.67M (−2.2%) |
| Kiwipete `perft 3` | 73.30M | 72.87M (−0.6%) |

The nodes and the moves are the same. In `psqt_delta`, it stopped
clang from inlining the function into `make_move` and `undo_move`, and
perft took 7% more steps, so that product stays. The MEGA65 builds
need 6 words more. In `tests/screen.keys` level 1 plays e2e4 at depth
6 again, which it now reaches within its time.

## 2026-11-04: A history table, and late move reductions

The quiet moves after the killers are tried by their history, where
they were in the order generated: a table by piece and target square,
to which a quiet move that cuts a node adds depth * depth. All are
halved when one reaches 65536, and cleared at each search, as the
killers are. A late move reduction searches a quiet move one ply
shallower (`LMR_R`) when it comes after the first three moves of a
node (`LMR_MIN_MOVES`), at depth 3 or more (`LMR_MIN_DEPTH`), out of
check, is not a killer, and does not give check. If it beats alpha,
it is searched again at the full depth.

Steps in niceemu V1.2, the text front end at `-O 2`:

| Run | Null move | History and LMR |
|---|---|---|
| 24 Bratko-Kopec positions, `go depth 4` | 1091.6M | 954.9M (−13%) |
| 24 Bratko-Kopec positions, `go depth 5` | 3518.2M | 2207.3M (−37%) |
| `go` from the opening, to depth 7 | 286M, 91073 nodes | 174M, 44334 nodes |

The steps a node are 2.6% more, 2759 against 2690 on the Bratko-Kopec
positions at depth 4: the order by the history, and the test for a
check of each late move. The 300 WAC positions on the host, in nodes:

| Run | Null move | History | History and LMR |
|---|---|---|---|
| `go depth 5` | 10.61M | 10.13M (−4.5%) | 7.12M (−33%) |
| `go depth 6` | 28.90M | 27.08M (−6.3%) | 17.68M (−39%) |

With LMR, the move is the same in 276 and 281 of them. The search
suite solves, at 93000 nodes a position, 317, 319, and 320 of 548
(WAC 229, 228, and 236; sbd 70, 71, and 62), and at 10000 nodes 249,
against 242. The MEGA65 builds need 76641 and 75215 words, 631 and
648 more, all of it code: the table is in the linear memory, which is
there already. Clearing the table with nested loops cost 1500 words
more, as they were unrolled; one loop over it, which also halves it,
does not.

Games of the host builds against the build before, both at 93000
nodes a move, from 8moves_v3.pgn, each opening with both colors,
cutechess-cli 1.5.1:

| Build | Games | Wins, losses, draws | Score | Elo |
|---|---|---|---|---|
| History | 2000 | 778, 630, 592 | 53.7% | +26 ± 13 |
| History and LMR | 2000 | 882, 590, 528 | 57.3% | +51 ± 13 |

The game of `tests/screen_game.keys`, recorded again with
`tools/screen_game.py`'s defaults, is again won by the engine at level
1, against the host build at 2000 nodes a move. In `tests/screen.keys`
level 1 now plays b1c3 at depth 5, where it played e2e4 at depth 6:
there, it does not reach depth 6 within the time of level 1.

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
