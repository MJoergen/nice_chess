# Release notes

The releases of nice_chess, newest first. Each section lists the changes
since the one before that a player or a user of the builds can see.
[doc/HISTORY.md](https://github.com/MJoergen/nice_chess/blob/main/doc/HISTORY.md)
has the full story, and
[doc/RELEASE.md](https://github.com/MJoergen/nice_chess/blob/main/doc/RELEASE.md)
how a release is made.

A release is named `Vmajor.minor`, and its files are
`nice_chess-Vmajor.minor.tar.gz` and `SHA256SUMS`
([README.md](README.md#download)). Each section names the nice_cpu tag
the release is built with, and the NiceMonMEGA65 release it was
checked on.

## V1.1 (2026-11-13)

A stronger engine, time controls, strengths, and pondering.

**What it needs**

- The MEGA65 builds need the monitor of NiceMonMEGA65 V2.0 or later,
  as they reach the memory through the byte windows, which the monitor
  has from V2.0 on. From the SD card, as before: `load CHESS.BIN`, then
  `r`.
- The builds for niceemu need niceemu of nice_cpu V2.0 or later, the
  window build for the windowed front end.
- The host build is for x86_64 Linux, linked statically.

**The engine**

- PeSTO's material and piece-square tables, for the middle game and
  the end game: about 140 Elo over V1.0's at the same nodes.
- A faster move generator, and the builds with nice_cpu V2.0's
  `--trust-alignment` and `--byte-windows`: the board searches about
  172000 nodes in 5 seconds, against 93000, and reaches depth 8 from
  the opening.
- A mop-up term against a bare king: the engine mates with king and
  rook, or king and queen, and more often with bishop and knight.
- The opening book has 184 lines and 620 positions, against 122 and
  403: answers to the moves Stockfish played out of it.
- Noise in the evaluation, to play weaker: the levels below 5, and the
  strengths below.

**The windowed front end**

- It starts at level 1, the weakest. Levels 1 to 5 now span about 790
  to 2240 Elo on the CCRL 40/4 scale, by `make rate` on 2026-11-12.
- Time controls, by T: none, Blitz 3+2, Rapid 15+10, and Classical
  90+30. A side whose clock reaches 0 loses.
- Under a time control, + and − set a strength, 1 to 5: about 1280 to
  2080 Elo at Blitz.
- Pondering, on at first, and P turns it off.
- Q clears the screen before it returns to the monitor.

**The text front end**

- In UCI: pondering (`go ponder`, `ponderhit`), and the options
  `Strength`, 1 to 5, and `Noise`, 0 to 1000.
- A search with no limit, `go infinite` or `go ponder`, ends at `quit`
  and at the end of the input.

**The board as a lichess bot**

- `tools/screen_uci.py` plays the windowed front end on the board as a
  UCI engine, through its keys, so the board's screen shows the game.
  `tools/lichess_engine.sh` runs it for lichess-bot, with the template
  `tools/lichess-bot.yml`. [BOT.md](BOT.md) says how.

Built with nice_cpu V2.0, and checked on a MEGA65 R3 under
NiceMonMEGA65 V2.0.1: `make board-test` passed, and the board won both
games of `make board-match`.

## V1.0 (2026-11-09)

The first release.

**What it needs**

- The MEGA65 builds run under the monitor of NiceMonMEGA65 V1.2.1 or
  later, which gives a program 92K words. The windowed front end draws
  its pieces in the font RAM, which NiceMonMEGA65 has from V1.6 on.
  Under an earlier monitor its squares show no pieces.
- From NiceMonMEGA65 V1.9 on, the monitor loads the MEGA65 builds from
  the folder `NICE` of a FAT32 SD card: `load CHESS.BIN`, then `r`.
- The builds for niceemu need niceemu of nice_cpu V1.6 or later, the
  window build for the windowed front end.
- The host build is for x86_64 Linux, linked statically.

**The engine**

- A port of mchess2: move generation checked by perft, an evaluation by
  material and piece-square tables, and a search by iterative deepening
  with a principal variation search, null-move pruning, late move
  reductions, quiescence, a transposition table, and a history table.
- An opening book of 403 positions.
- On the MEGA65 it takes 5 seconds a move at level 3, and reaches depth
  7 from the opening. At the board's speed, `make rate` gave levels 1
  to 5 about 1600, 1770, 1850, 1980, and 2050 Elo on the CCRL 40/4
  scale on 2026-11-09.

**The front ends**

- The text front end: commands on the console, and UCI after `uci`,
  with the option `OwnBook`.
- The windowed front end, on the 80×25 screen: moves by the arrow keys
  or typed, undo, switch sides, rotate, new game, the level, the book,
  and a clock for each side.
- Both show the version: the banner, the UCI `id name`, and the title
  of the window.
- The archive has the MEGA65 builds as `mega65/CHESS.BIN`, the windowed
  front end, and `mega65/CHESST.BIN`, the text front end.

Built with nice_cpu V1.6, and checked on the MEGA65 under NiceMonMEGA65
V1.9: `make board-test` passed, and the board won both games of
`make board-match`.
