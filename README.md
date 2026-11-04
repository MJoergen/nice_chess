# nice_chess

[![CI](https://github.com/MJoergen/nice_chess/actions/workflows/test.yml/badge.svg)](https://github.com/MJoergen/nice_chess/actions/workflows/test.yml)

A simple chess engine for the NICE system

It is a port of the [mchess2](https://github.com/MJoergen/mchess2)
engine; [doc/HISTORY.md](doc/HISTORY.md) tells how it was done.

![The windowed front end on the MEGA65, after the engine mated as black](doc/screenshot.png)

## Playing

Build with `make deps all`; [CONTRIBUTING.md](CONTRIBUTING.md) lists
what that needs, and the tests.

To play in the emulator:

    deps/niceemu-V1.2-x86_64-linux/niceemu build/nice/chess_text.bin

Type `help` for the commands. Moves are in coordinate notation, such as
`e2e4`, and `go` makes the engine move. In niceemu, `go` takes 300 million
steps, about 7 seconds in the emulator and 4 on the board; `go depth 4`
or `go nodes 20000` are quicker. `book on` makes `go` play from the
opening book, where the position is in it, and `book` lists its moves.

The windowed front end shows the board on the 80×25 screen. It needs
the window build of niceemu, `niceemu-window-V1.2-x86_64-linux.tar.gz`
from the [V1.2 release](https://github.com/MJoergen/nice_cpu/releases/tag/V1.2):

    niceemu --window --speed 0 build/nice/chess_screen.bin

`--speed 0` runs the emulator as fast as it can; at its default of one
step per microsecond, a move takes the engine five minutes. Move with
the arrows and Enter, or type a move such as `e2e4` and Enter. The keys
are on the screen: U undo, S switch sides, R rotate, N new game, + and −
the level, O the opening book on or off, Q quit, and Esc to make the
engine move now. The book is on at first, and the engine's first moves
come from it, at random by their weights.

The host build speaks UCI after `uci`, so a chess GUI can play it.
Its option `OwnBook` turns the book on; it is off at first.
`tools/match.py` plays two UCI engines against each other, and can run
the NICE build in niceemu, or on the board, as one of them.
`make rate` rates the engine at the board's speed against Stockfish's
UCI_Elo levels, with cutechess-cli and Ordo (CONTRIBUTING.md).

## On the MEGA65

The builds in `build/mega65/` run on the MEGA65 under the monitor of
[NiceMonMEGA65](https://github.com/MJoergen/NiceMonMEGA65), from its
release V1.2.1 on, which gives a program 92K words of RAM. They
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

`tools/board_uci.py --eth IP build/mega65/chess_text.bin` loads the
text front end into the board and is then a UCI engine on the host, so
cutechess-cli, a chess GUI, or lichess-bot can play the board against
other engines. Its docstring has a cutechess-cli example; there,
`dir=.` keeps the path of the .bin relative to the current directory. The board's
clock is real, and one board plays one game at a time.
