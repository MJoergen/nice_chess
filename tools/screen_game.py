#!/usr/bin/env python3
"""screen_game.py - plays a game against the windowed front end, by its keys.

Usage: screen_game.py [options] SCREEN_BIN HOST_ENGINE KEYS_OUT

Runs SCREEN_BIN (build/nice/chess_screen.bin) in niceemu ($NICEEMU, or
--emu), with no window, and plays white against it through the keys
the player would press. HOST_ENGINE (build/host/chess_text, in UCI)
chooses white's moves, at --nodes a move. The moves go in two ways in
turn: typed, as "e2e4" and Enter, and with the cursor, as arrows to
the piece, Enter, arrows to its square, and Enter. Under-promotions
are always typed.

The front end writes each move to the console as "move e2e4", which is
how this script learns black's moves. A second HOST_ENGINE, in its text
mode, keeps the game and says when it has ended (as in match.py). Then,
or after --max-moves moves, the script presses Q.

KEYS_OUT gets every key pressed, so that the same game can be played
again from a file: niceemu -i KEYS_OUT SCREEN_BIN, or with --window.
As niceemu's clock counts steps, the engine plays the same moves again.

Exits with 1 if the front end makes a move other than the one pressed,
if a move is illegal, or if the front end does not halt at the end.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys

from match import Referee

UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"


class Player:
    """The host engine, in UCI."""

    def __init__(self, cmd: str, nodes: int):
        self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)
        self.nodes = nodes
        self.send("uci")
        self.wait("uciok")

    def send(self, s: str) -> None:
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def wait(self, prefix: str) -> str:
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("the host engine has quit")
            if line.startswith(prefix):
                return line.strip()

    def best(self, moves: list[str]) -> str:
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send(f"go nodes {self.nodes}")
        return self.wait("bestmove").split()[1]


class Screen:
    """The front end in niceemu, fed key by key."""

    def __init__(self, emu: str, binfile: str):
        self.p = subprocess.Popen([emu, binfile], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)
        self.keys: list[str] = []

    def press(self, *keys: str) -> None:
        for k in keys:
            self.keys.append(k)
            self.p.stdin.write(k)
        self.p.stdin.flush()

    def line(self) -> str:
        """The next line of the console that is a move or a result."""
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("the front end has quit")
            if line.startswith(("move ", "result ")):
                return line.strip()


def square(s: str) -> tuple[int, int]:
    return ord(s[0]) - ord("a"), ord(s[1]) - ord("1")


def arrows(frm: tuple[int, int], to: tuple[int, int]) -> list[str]:
    df, dr = to[0] - frm[0], to[1] - frm[1]
    return [RIGHT if df > 0 else LEFT] * abs(df) + [UP if dr > 0 else DOWN] * abs(dr)


def main() -> None:
    ap = argparse.ArgumentParser(description="Plays the windowed front end by its keys.")
    ap.add_argument("screen_bin")
    ap.add_argument("host_engine")
    ap.add_argument("keys_out")
    ap.add_argument("--emu", default=os.environ.get("NICEEMU", "niceemu"))
    ap.add_argument("--nodes", type=int, default=2000, help="white's nodes a move")
    ap.add_argument("--level", type=int, default=1, help="the engine's level, 1 to 5")
    ap.add_argument("--max-moves", type=int, default=150, help="full moves before Q")
    args = ap.parse_args()

    player = Player(args.host_engine, args.nodes)
    referee = Referee(args.host_engine)
    screen = Screen(args.emu, args.screen_bin)
    screen.press(*(["-"] * (3 - args.level) + ["+"] * (args.level - 3)))
    cursor = (4, 1)  # e2, where the front end starts it
    moves: list[str] = []
    result = None
    try:
        while len(moves) < 2 * args.max_moves:
            m = player.best(moves)
            if len(moves) % 4 == 0 or len(m) == 5:
                screen.press(*m, "\n")
            else:
                frm, to = square(m[:2]), square(m[2:4])
                screen.press(*arrows(cursor, frm), "\n", *arrows(frm, to), "\n")
                cursor = to
            line = screen.line()
            if line != f"move {m}":
                sys.exit(f"screen_game.py: white played {m}, the front end says: {line}")
            moves.append(m)
            result = referee.move(m)
            if result == "illegal":
                sys.exit(f"screen_game.py: white's move {m} is illegal")
            if result:
                break
            line = screen.line()
            if not line.startswith("move "):
                sys.exit(f"screen_game.py: the front end says: {line}")
            moves.append(line.split()[1])
            print(f"{len(moves) // 2:3d}. {moves[-2]} {moves[-1]}", flush=True)
            result = referee.move(moves[-1])
            if result == "illegal":
                sys.exit(f"screen_game.py: black's move {moves[-1]} is illegal")
            if result:
                break
    finally:
        screen.press("q")
        with open(args.keys_out, "w", encoding="latin-1", newline="") as f:
            f.write("".join(screen.keys))
        screen.p.stdin.close()
        out = screen.p.stdout.read()
        screen.p.wait()
        player.send("quit")
        referee.close()
    print(" ".join(moves))
    print(result or f"no result after {args.max_moves} moves")
    if "Halted (HALT)" not in out:
        sys.exit("screen_game.py: the front end did not halt normally:\n" + out[-1000:])


if __name__ == "__main__":
    main()
