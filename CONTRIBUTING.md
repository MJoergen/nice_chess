# Contributing

This file is for anyone who changes the repository: how to build it and
run the tests.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`.

    make             # list the targets
    make deps        # nice_cpu V1.6 and niceemu V1.6, and mchess2's test suites, into deps/
    make all         # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/chess_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 5 on the host (about a minute)
    make screen-game # a full game in the windowed front end, in niceemu (a minute)
    make match-random   # the host build against random moves
    make match-mchess2  # the host build against mchess2 (about 15 minutes)
    make openings    # the openings of make rate, 8moves_v3.pgn, into deps/books/
    make rate        # a rating against Stockfish's UCI_Elo levels (hours; see below)
    make size        # the size of the MEGA65 builds, against the 92K words the monitor leaves them
    make pieces      # draw the pieces again into src/ui/pieces.h, after a change to tools/mkpieces.py (needs Pillow)

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

`make rate` needs cutechess-cli, Ordo, and Stockfish (`CUTECHESS`,
`ORDO`, `STOCKFISH`). Its openings are `BOOK`, a .pgn or .epd file:
by default `deps/books/8moves_v3.pgn`, from fishtest's books at a
pinned commit, which it fetches (`make openings`). It plays `build/host/chess_fast`, the host build
without asserts, at `NODES` nodes a move, 93000 by default, which is
about 5 seconds a move on the MEGA65. At the same nodes the host and
NICE builds play the same moves. The opponents are Stockfish at each
UCI_Elo of `ELOS` (1700 1900 2100), at 60+0.6, `GAMES` (200) games
each, `CONCURRENCY` (4) at a time. Ordo anchors each Stockfish at its
UCI_Elo, which is on the CCRL 40/4 scale. The games and the rating are
in `build/rate/`.

GitHub Actions runs `make test`, `make perft-deep`, `make screen-game`,
`make match-random`, and `make size` on every push
([.github/workflows/test.yml](.github/workflows/test.yml)). The tests
on the board are in [README.md](README.md#on-the-mega65).
