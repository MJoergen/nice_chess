# nice_chess
A simple chess engine for the NICE system

See [doc/PORTING_PLAN.md](doc/PORTING_PLAN.md) for the plan to port the
[mchess2](https://github.com/MJoergen/mchess2) engine to the NICE system.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`.

    make deps        # nice_cpu V1.2 and niceemu V1.2, and mchess2's test suites, into deps/
    make             # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/chess_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 5 on the host (about a minute)
    make screen-game # a full game in the windowed front end, in niceemu (a minute)
    make match-random   # the host build against random moves
    make match-mchess2  # the host build against mchess2 (about 15 minutes)
    make size        # the size of the MEGA65 builds, against the 92K words the monitor leaves them

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

To play in the emulator:

    deps/niceemu-V1.2-x86_64-linux/niceemu build/nice/chess_text.bin

Type `help` for the commands. Moves are in coordinate notation, such as
`e2e4`, and `go` makes the engine move. In niceemu, `go` takes 300 million
steps, about 7 seconds in the emulator and 4 on the board; `go depth 4`
or `go nodes 20000` are quicker.

The windowed front end shows the board on the 80×25 screen. It needs
the window build of niceemu, `niceemu-window-V1.2-x86_64-linux.tar.gz`
from the [V1.2 release](https://github.com/MJoergen/nice_cpu/releases/tag/V1.2):

    niceemu --window --speed 0 build/nice/chess_screen.bin

`--speed 0` runs the emulator as fast as it can; at its default of one
step per microsecond, a move takes the engine five minutes. Move with
the arrows and Enter, or type a move such as `e2e4` and Enter. The keys
are on the screen: U undo, S switch sides, R rotate, N new game, + and −
the level, Q quit, and Esc to make the engine move now.

The host build speaks UCI after `uci`, so a chess GUI can play it.
`tools/match.py` plays two UCI engines against each other, and can run
the NICE build in niceemu, or on the board, as one of them.

## On the MEGA65

The builds in `build/mega65/` run on the MEGA65 under the monitor of
[NiceMonMEGA65](https://github.com/MJoergen/NiceMonMEGA65), from its
branch `dev-V1.2.1` on, which gives a program 92K words of RAM. They
are loaded over the board's Ethernet port or its debug UART with
NiceMonMEGA65's tools, from a checkout of it (`NICEMON=DIR`,
`../NiceMonMEGA65` by default), with `ETH=IP` or `PORT=/dev/ttyUSBn`:

    make board-screen ETH=192.168.1.65   # the windowed front end, on the board's screen and keyboard
    make board-text ETH=192.168.1.65     # the text front end; then nc 192.168.1.65 4510 is its terminal
    make board-test ETH=192.168.1.65     # the tests of both front ends, on the board
    make board-game ETH=192.168.1.65     # a full game by the keys, through the link (a few minutes)
    make board-match ETH=192.168.1.65    # two UCI games against the host build (about 15 minutes)

On the board the engine takes 5 seconds for a move at level 3, where it
reaches depth 5 from the opening; the board runs about 54 million steps
of the engine a second. The keys are those of the window, on the
MEGA65's keyboard: the cursor keys, RETURN, INST/DEL, and ESC. Q clears
the screen and returns to the monitor.
