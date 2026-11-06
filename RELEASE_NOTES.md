# Release notes

The releases of nice_chess, newest first. Each section lists the changes
since the one before that a player or a user of the builds can see.
[doc/HISTORY.md](doc/HISTORY.md) has the full story, and
[doc/RELEASE.md](doc/RELEASE.md) how a release is made.

A release is named `Vmajor.minor`, and its files are
`nice_chess-Vmajor.minor.tar.gz` and `SHA256SUMS`
([README.md](README.md#download)). Each section names the nice_cpu tag
the release is built with, and the NiceMonMEGA65 release it was
checked on.

## V1.0 (not yet released)

The first release.

**What it needs.**

- The MEGA65 builds run under the monitor of NiceMonMEGA65 V1.2.1 or
  later, which gives a program 92K words. The windowed front end draws
  its pieces in the font RAM, which NiceMonMEGA65 has from V1.6 on.
  Under an earlier monitor its squares show no pieces.
- The builds for niceemu need niceemu of nice_cpu V1.6 or later, the
  window build for the windowed front end.
- The host build is for x86_64 Linux.

**The engine**

- A port of mchess2: move generation checked by perft, an evaluation by
  material and piece-square tables, and a search by iterative deepening
  with a principal variation search, null-move pruning, late move
  reductions, quiescence, a transposition table, and a history table.
- An opening book of 403 positions.
- On the MEGA65 it takes 5 seconds a move at level 3, and reaches depth
  5 from the opening.

**The front ends**

- The text front end: commands on the console, and UCI after `uci`,
  with the option `OwnBook`.
- The windowed front end, on the 80×25 screen: moves by the arrow keys
  or typed, undo, switch sides, rotate, new game, the level, the book,
  and a clock for each side.
- Both show the version: the banner, the UCI `id name`, and the title
  of the window.

Built with nice_cpu V1.6.
