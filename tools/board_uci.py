#!/usr/bin/env python3
"""board_uci.py - the engine on the MEGA65, as a UCI engine of the host.

Usage: board_uci.py [--nicemon DIR] (--eth HOST | --port DEVICE) PROG.bin

Loads PROG.bin, the text front end built for the monitor
(build/mega65/chess_text.bin), into the board through board.py, and is
then a UCI engine: what comes in on stdin goes to the program on the
board, and the lines it prints come out on stdout. So cutechess-cli, a
chess GUI, or lichess-bot can play the board as it plays any engine:

    cutechess-cli -engine name=mega65 cmd=tools/board_uci.py dir=. proto=uci \\
        arg=--eth arg=192.168.1.65 arg=build/mega65/chess_text.bin tc=40/200 \\
      -engine cmd=stockfish proto=uci tc=60+0.6 \\
        option.UCI_LimitStrength=true option.UCI_Elo=1800 ...

cutechess-cli runs an engine in the directory of its cmd, so dir=.
keeps the path of PROG.bin relative to the current directory.
option.OwnBook=true plays the first moves from the engine's book.

The board's clock is real, and its time management is its own, from
wtime and btime. The time a move takes on the link counts against it,
so a time control with a margin, or movetime, suits it. One board plays
one game at a time. The program does not read its input while it
searches, so stop does not cut a search short.

The prompts "> " of the program are taken off its lines, and the
monitor's "Returned" line is not passed on. On quit, or the end of
stdin, the program is sent quit, and board_uci.py waits for it to
return to the monitor, for up to 10 seconds. What the load prints
goes to stderr.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import select
import sys
import time

from board import Board


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--nicemon", help="a checkout of NiceMonMEGA65 (default $NICEMON)")
    ap.add_argument("--eth", metavar="HOST[:PORT]", help="the board's Ethernet port")
    ap.add_argument("--port", metavar="DEVICE", help="the board's debug UART")
    ap.add_argument("bin")
    args = ap.parse_args()
    link = ["--eth", args.eth] if args.eth else ["--port", args.port] if args.port else []
    if not link:
        ap.error("--eth HOST or --port DEVICE is needed")

    # stdout is the UCI stream, so nothing else may go there.
    with contextlib.redirect_stdout(sys.stderr):
        board = Board(args.nicemon, link, args.bin)

    stdin = sys.stdin.fileno()
    pending = b""
    quitting = None  # When quit was sent
    while not board.returned:
        if quitting and time.monotonic() - quitting > 10:
            print("board_uci.py: the program did not return after quit", file=sys.stderr)
            break
        # The host's lines to the board, as they come.
        if not quitting and select.select([stdin], [], [], 0)[0]:
            data = os.read(stdin, 4096)
            if not data:
                data = b"quit\n"
            pending += data
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                line = line.rstrip(b"\r")
                board.write(line + b"\n")
                if line.strip() == b"quit":
                    quitting = time.monotonic()
                    break
        # The board's lines to the host.
        line = board.readline(0.01)
        if line is None:
            continue
        while line.startswith("> "):
            line = line[2:]
        if line and not line.startswith("Returned "):
            sys.stdout.write(line + "\n")
            sys.stdout.flush()

    board.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
