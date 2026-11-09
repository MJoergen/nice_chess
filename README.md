# nice_chess

[![CI](https://github.com/MJoergen/nice_chess/actions/workflows/test.yml/badge.svg)](https://github.com/MJoergen/nice_chess/actions/workflows/test.yml)

A simple chess engine for the NICE system

It is a port of the [mchess2](https://github.com/MJoergen/mchess2)
engine; [doc/HISTORY.md](https://github.com/MJoergen/nice_chess/blob/main/doc/HISTORY.md)
tells how it was done.

![The windowed front end in niceemu, after the engine mated as black](doc/screenshot.png)

## Download

Each [release](https://github.com/MJoergen/nice_chess/releases) has
`nice_chess-TAG.tar.gz`, where TAG is the release, such as `V1.0`, and
`SHA256SUMS`. [RELEASE_NOTES.md](RELEASE_NOTES.md) says what changed
in each, and what it needs. The archive's directory has:

- `mega65/`: both front ends for the MEGA65 (On the MEGA65, below):
  `CHESS.BIN`, the windowed front end, and `CHESST.BIN`, the text
  front end. They have 8.3 names, so the monitor loads them from the SD
  card.
- `niceemu/`: both front ends for niceemu (Playing, below).
- `linux-x86_64/nice_chess`: the text front end on the host, linked
  statically: the same commands at a terminal, and a UCI engine for a
  chess GUI.

The front ends show their version: the banner of the text front end,
its UCI `id name`, and the title of the window.

## Playing

The front ends for niceemu run in the niceemu of nice_cpu's
[V2.0 release](https://github.com/MJoergen/nice_cpu/releases/tag/V2.0):
`niceemu-V2.0-x86_64-linux.tar.gz` runs the text front end, and the
window build, `niceemu-window-V2.0-x86_64-linux.tar.gz`, runs both.
From the archive:

    niceemu --step-time niceemu/chess_text.bin
    niceemu --window --step-time niceemu/chess_screen.bin

From a checkout, `make deps all` fetches niceemu into `deps/` and
builds the front ends into `build/nice/`;
[CONTRIBUTING.md](https://github.com/MJoergen/nice_chess/blob/main/CONTRIBUTING.md)
lists what that needs, and the tests:

    deps/niceemu-V2.0-x86_64-linux/niceemu --step-time build/nice/chess_text.bin

`--step-time` makes niceemu's clock count steps, one microsecond each,
rather than the host's time; the window runs as fast as the host can.
It is optional for playing. Without it, the engine takes niceemu for a
board: `go` takes 5 seconds of the host's time, the clocks show the
host's time, and the moves depend on the host's speed. The tests and
the tools run niceemu with it, so that the runs are deterministic.

In the text front end, type `help` for the commands. Moves are in
coordinate notation, such as `e2e4` or `e7e8q`, and `go` makes the
engine move. In niceemu with `--step-time`, `go` takes 300 million
steps, about 7 seconds in the emulator; on the board it takes 5
seconds, and on the host 1 second. `go depth 4` or `go nodes 20000`
are quicker. `book on` makes `go` play from the opening book, where
the position is in it, and `book` lists its moves.

The windowed front end shows the board on the 80×25 screen. Move with
the arrows and Enter, or type a move such as `e2e4` and Enter. A
promotion by the arrows is to a queen; for another piece, type the
move, such as `e7e8n`. The keys are on the screen: U undo, S switch
sides, R rotate, N new game, + and − the level, O the opening book on
or off, Q quit, and Esc to make the engine move now. Space is Enter,
Backspace erases a typed move, and Esc also drops a piece picked up or
a typed move. The book is on at first, and the engine's first moves
come from it, at random by their weights; in niceemu the seed is
fixed, so the choice is the same every run.

There are 5 levels, and the game starts at level 3. A level sets only
the time the engine takes for a move: 1/9, 1/3, 1, 3, or 9 times that
of `go`. On the board that is about 0.6, 1.7, 5, 15, and 45 seconds,
and about 1600, 1770, 1850, 1980, and 2050 Elo on the CCRL 40/4 scale,
as `make rate` measured them on 2026-11-09 (below). The engine plays a
move from the book at once, at any level.

Each side has a clock, which counts up the time it has taken; there is
no time control, and N sets both to 0. On the board the clocks show
real time. In niceemu with `--step-time` they count steps, at 54 million
a second, the board's rate, so they show the time the board would take.

The host build has the same commands at a terminal, and speaks UCI
after `uci`, so a chess GUI can play it. Its option `OwnBook` turns
the book on; it is off at first.
`tools/match.py` plays two UCI engines against each other, and can run
the NICE build in niceemu, or on the board, as one of them.
`make rate` rates each of the 5 levels at the board's speed against
Stockfish's UCI_Elo levels, with cutechess-cli and Ordo
(CONTRIBUTING.md); the figures above are its result of 2026-11-09
([LOG.md](https://github.com/MJoergen/nice_chess/blob/main/LOG.md)).

## On the MEGA65

The builds in `build/mega65/` run on the MEGA65 under the monitor of
[NiceMonMEGA65](https://github.com/MJoergen/NiceMonMEGA65), from its
release V1.2.1 on, which gives a program 92K words of RAM.

From NiceMonMEGA65 V1.9 on, the monitor loads a program from a FAT32
SD card, so the board needs no host. On a PC, make the folder `NICE`
in the root of the card and copy `CHESS.BIN` from the release into it
(or `build/mega65/chess_screen.bin`, renamed to an 8.3 name). Put the
card in the MEGA65 and type at the monitor's prompt:

    load CHESS.BIN
    r

`CHESST.BIN`, the text front end, loads the same way; its terminal is
the debug UART, or `nc IP 4510` over Ethernet.

From a host, the builds are loaded over the board's Ethernet port or its debug UART with
NiceMonMEGA65's tools, from a checkout of it (`NICEMON=DIR`,
`../NiceMonMEGA65` by default), with `ETH=IP` or `PORT=/dev/ttyUSBn`:

    make board-screen ETH=192.168.1.65   # the windowed front end, on the board's screen and keyboard
    make board-text ETH=192.168.1.65     # the text front end; then nc 192.168.1.65 4510 is its terminal
    make board-test ETH=192.168.1.65     # the tests of both front ends, on the board
    make board-game ETH=192.168.1.65     # a full game by the keys, through the link (a few minutes)
    make board-match ETH=192.168.1.65    # two UCI games against the host build (about 15 minutes)
    make board-stockfish ETH=192.168.1.65  # a game on the board's screen against Stockfish

On the board the engine takes 5 seconds for a move at level 3, where it
reaches depth 7 from the opening; the board runs about 54 million steps
of the engine a second. The keys are those of the window, on the
MEGA65's keyboard: the cursor keys, RETURN, INST/DEL, and ESC. Q clears
the screen and returns to the monitor.

The windowed front end draws its pieces as glyphs in the font RAM, which
NiceMonMEGA65 has from its release V1.6 on
([NiceMonMEGA65#71](https://github.com/MJoergen/NiceMonMEGA65/issues/71)).
Under an earlier monitor, the squares show no pieces.

`make board-stockfish` plays the windowed front end on the board
against Stockfish (`STOCKFISH`) at UCI_Elo `ELO` (1800) and a second a
move, by its keys, through `tools/screen_game.py`; the board's screen
shows the game as it is played. Stockfish is white, or black with
`BLACK=1`. The host prints each move, with the score of a second
Stockfish, at its full strength, from white's side.

`tools/board_uci.py --eth IP build/mega65/chess_text.bin` loads the
text front end into the board and is then a UCI engine on the host, so
cutechess-cli, a chess GUI, or lichess-bot can play the board against
other engines. Its docstring has a cutechess-cli example; there,
`dir=.` keeps the path of the .bin relative to the current directory. The board's
clock is real, and one board plays one game at a time.

## License

MIT, in [LICENSE](LICENSE).
