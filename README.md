# nice_chess
A simple chess engine for the NICE system

See [doc/PORTING_PLAN.md](doc/PORTING_PLAN.md) for the plan to port the
[mchess2](https://github.com/MJoergen/mchess2) engine to the NICE system.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
`git`, and `curl`.

    make deps    # clone nice_cpu V1.2 into deps/ and download niceemu V1.2
    make test    # build build/mchess.bin and run the tests in niceemu

To use local copies instead, pass `NICE_CPU=DIR` and `NICEEMU=PATH` to
make.
