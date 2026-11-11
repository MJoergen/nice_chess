# Contributing

This file is for anyone who changes the repository: how to build it and
run the tests.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`. `make match-mchess2`
needs a C++ compiler too. `make rate` and `make dist` link the host
build without asserts statically, which needs glibc's static library:
`libc6-dev` has it on Debian and Ubuntu, `glibc-static` on Fedora.

    make             # list the targets
    make deps        # nice_cpu V2.0 and niceemu V2.0, and mchess2's test suites, into deps/
    make all         # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/chess_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 5 on the host (about a minute)
    make screen-game # a full game in the windowed front end, in niceemu (a minute)
    make match-random   # the host build against random moves
    make match-mchess2  # the host build against mchess2 (about 15 minutes)
    make openings    # the openings of make rate, 8moves_v3.pgn, into deps/books/
    make rate        # a rating of each level against Stockfish's UCI_Elo levels (hours; see below)
    make size        # the size of the MEGA65 builds, against the 92K words the monitor leaves them
    make profile     # the steps of each function in niceemu, on the 24 Bratko-Kopec positions (half a minute)
    make pieces      # draw the pieces again into src/ui/pieces.h, after a change to tools/mkpieces.py (needs Pillow)
    make dist        # the archive of a release, and SHA256SUMS, in build/dist/

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

`make rate` needs cutechess-cli, Ordo, and Stockfish (`CUTECHESS`,
`ORDO`, `STOCKFISH`). Its openings are `BOOK`, a .pgn or .epd file:
by default `deps/books/8moves_v3.pgn`, from fishtest's books at a
pinned commit, which it fetches (`make openings`). It plays
`build/host/chess_fast`, the host build without asserts, at each level
of `LEVELS` (1 2 3 4 5), at a fixed number of nodes a move: `NODES`
(93000) is about 5 seconds a move on the MEGA65, the time of level 3,
and levels 1, 2, 4, and 5 get 1/9, 1/3, 3, and 9 times that, their
share of the time. At the same nodes the host and NICE builds play the
same moves. The opponents are Stockfish at each UCI_Elo of `ELOS`
(1600 1800 2000 2200), at 60+0.6, `GAMES` (100) games for each level
against each, `CONCURRENCY` (4) at a time: 2000 games by default,
about 6 hours at `CONCURRENCY=8`.
`STRENGTHS` (empty by default), such as `1 2 3 4 5`, rates the
strengths of a time control too, with the UCI option `Strength`, at
3/2 of `NODES` a move: about 7.5 seconds on the MEGA65, the engine's
time for its first move at Blitz 3+2. `LEVELS=` rates no level.
`OPPONENTS` (empty by default), such as
`blunders=1266=/path/to/blunders`, adds other UCI engines, each
anchored at its rating on the CCRL 40/4 list; Stockfish's UCI_Elo goes
no lower than 1320. Such an engine plays on its clock, on a host
faster than CCRL's, so it can play above its rating (LOG.md).
Ordo rates the levels and the strengths in one pool, with each
Stockfish anchored at its UCI_Elo, which is on the CCRL 40/4 scale.
The rating is in `build/rate/ratings.txt`.

Every match writes its games, in PGN, to a file of its own in `games/`
(not in git; `PGNDIR=DIR` for another directory), named by the target
and the time it started, such as `games/rate-20261111-143000.pgn`:
`make rate`, `make match-random`, `make match-mchess2`, and the board's
`board-match`, `board-game`, and `board-stockfish`. `board-stockfish`
puts Stockfish's score after each move, as a comment. For a match
run by hand, `tools/match.py` and `tools/screen_game.py` take
`--pgn FILE`, and cutechess-cli `-pgnout FILE`.

`make test` also runs the tests of `tools/release.py`
(`tools/test_release.py`).

The front ends show `VERSION`: the release tag on a tagged commit, and
what `git describe` says between tags. `build/gen/version.c` holds it.
`tools/runtest.sh` replaces it with `VERSION` in the output, so the
`*.expected` files do not depend on it.

GitHub Actions runs `make test`, `make perft-deep`, `make screen-game`,
`make match-random`, and `make size` on every push
([.github/workflows/test.yml](.github/workflows/test.yml)). The tests
on the board are in [README.md](README.md#the-board-from-a-host).

## Releases

[doc/RELEASE.md](doc/RELEASE.md) is how to make a release, with
`tools/release.py` and `make release-check`, which runs `make
board-test` and `make board-match` on the MEGA65. The push of a tag
starts [release.yml](.github/workflows/release.yml), which builds the
archive and creates the GitHub release.
[RELEASE_NOTES.md](RELEASE_NOTES.md) has what changed in each release.
