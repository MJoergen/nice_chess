# nice_chess
A simple chess engine for the NICE system

See [doc/PORTING_PLAN.md](doc/PORTING_PLAN.md) for the plan to port the
[mchess2](https://github.com/MJoergen/mchess2) engine to the NICE system.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`.

    make deps        # nice_cpu V1.2 and niceemu V1.2, and mchess2's perft suite, into deps/
    make             # build/nice/chess_text.bin for NICE, build/host/chess_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 5 on the host (about a minute)
    make size        # the size of the NICE build, against the 28K words of the board

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

To play in the emulator:

    deps/niceemu-V1.2-x86_64-linux/niceemu build/nice/chess_text.bin

Type `help` for the commands. `go` makes the engine move.
