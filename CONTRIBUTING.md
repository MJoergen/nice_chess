# Contributing

This file is for anyone who changes the repository: how to build it and
run the tests.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`.

    make             # list the targets
    make deps        # nice_cpu V1.2 and niceemu V1.2, and mchess2's test suites, into deps/
    make all         # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/chess_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 5 on the host (about a minute)
    make screen-game # a full game in the windowed front end, in niceemu (a minute)
    make match-random   # the host build against random moves
    make match-mchess2  # the host build against mchess2 (about 15 minutes)
    make size        # the size of the MEGA65 builds, against the 92K words the monitor leaves them

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

GitHub Actions runs `make test`, `make perft-deep`, `make screen-game`,
`make match-random`, and `make size` on every push
([.github/workflows/test.yml](.github/workflows/test.yml)). The tests
on the board are in [README.md](README.md#on-the-mega65).
