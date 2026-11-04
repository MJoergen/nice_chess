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
or after --max-moves moves, the script ends the front end's input,
which ends the program with the screen as it is.

--opponent CMD plays CMD, a UCI engine such as stockfish, instead of
HOST_ENGINE, which then only keeps the game; --opt NAME=VALUE sets
its options, such as UCI_Elo=1800, and --movetime MS gives it a time
a move instead of --nodes. --black has the opponent play black: the
script presses S first, so that the front end's engine plays white,
and R, so that white stays at the bottom, and then types each of
black's moves. So, with --eth, the board's
screen shows a game of the engine against another, as it is played.

--eval CMD scores the position after each move with CMD, a UCI
engine at its full strength, such as stockfish, for --eval-time MS
(500) a move, and prints each move with the score, in pawns from
white's side, or as "mate N" (N moves, + if white mates). It thinks
while the front end does, after the opponent's move.

KEYS_OUT gets every key pressed, so that the same game can be played
again from a file: niceemu -i KEYS_OUT SCREEN_BIN, or with --window.
As niceemu's clock counts steps, the engine plays the same moves again.

With --eth HOST or --port DEVICE, SCREEN_BIN (build/mega65/chess_screen.bin,
built for the monitor) runs on the MEGA65 instead, through board.py,
and --dump FILE saves the screen at the end for screenshot.py. The
board's clock is real, so the engine's moves differ from run to run.

Exits with 1 if the front end makes a move other than the one pressed,
if a move is illegal, or if the front end does not end normally.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
import time

from board import Board, dump_lines
from match import Referee

UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"


class Player:
    """The host engine, in UCI."""

    def __init__(self, cmd: str, go: str, options: list[str] | None = None):
        self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)
        self.go = go
        self.send("uci")
        self.wait("uciok")
        for opt in options or []:
            name, _, value = opt.partition("=")
            self.send(f"setoption name {name} value {value}")
        self.send("isready")
        self.wait("readyok")

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
        self.send(self.go)
        return self.wait("bestmove").split()[1]

    def score(self, moves: list[str]) -> str:
        """The score of the position after moves, from white's side."""
        self.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        self.send(self.go)
        score = "?"
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("the engine that scores has quit")
            w = line.split()
            if w[:1] == ["bestmove"]:
                return score
            if "score" in w:
                kind, n = w[w.index("score") + 1], int(w[w.index("score") + 2])
                if len(moves) % 2:
                    n = -n  # Black is to move
                score = f"{n / 100:+.2f}" if kind == "cp" else f"mate {n:+d}"


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

    def end(self, dump: str | None) -> bool:
        """Ends the input, and returns whether the front end halted."""
        self.p.stdin.close()
        out = self.p.stdout.read()
        self.p.wait()
        if "Halted (HALT)" not in out:
            print(out[-1000:], file=sys.stderr)
        return "Halted (HALT)" in out


class BoardScreen:
    """The front end on the board, through board.py."""

    def __init__(self, nicemon: str | None, link_args: list[str], binfile: str):
        self.board = Board(nicemon, link_args, binfile)
        self.keys: list[str] = []

    def press(self, *keys: str) -> None:
        self.keys += keys
        self.board.write("".join(keys).encode("latin-1"))

    def line(self) -> str:
        while True:
            line = self.board.readline(600)
            if line is None:
                raise RuntimeError("the front end on the board has quit, or is silent")
            if line.startswith(("move ", "result ")):
                return line

    def end(self, dump: str | None) -> bool:
        """Saves the screen, once the front end has drawn its last move,
        ends the input, and returns whether the program returned to the
        monitor."""
        if dump:
            end = time.monotonic() + 10
            while not self.board.quiet(0.5) and time.monotonic() < end:
                self.board.readline(0.5)
            with open(dump, "w") as f:
                f.write("\n".join(dump_lines(self.board.screen())) + "\n")
        self.board.end_input()
        self.board.wait_returned(10)
        self.board.close()
        return self.board.returned


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
    ap.add_argument("--max-moves", type=int, default=150, help="full moves before the end")
    ap.add_argument("--eth", metavar="HOST[:PORT]", help="play on the board, over its Ethernet port")
    ap.add_argument("--port", metavar="DEVICE", help="play on the board, over its debug UART")
    ap.add_argument("--nicemon", help="a checkout of NiceMonMEGA65 (default $NICEMON)")
    ap.add_argument("--dump", metavar="FILE", help="on the board, save the screen at the end")
    ap.add_argument("--opponent", metavar="CMD", help="the UCI engine that plays the front end")
    ap.add_argument("--opt", action="append", metavar="NAME=VALUE", help="a UCI option of it")
    ap.add_argument("--movetime", type=int, metavar="MS", help="its time a move, not --nodes")
    ap.add_argument("--black", action="store_true", help="the opponent plays black")
    ap.add_argument("--eval", metavar="CMD", help="a UCI engine that scores each move")
    ap.add_argument("--eval-time", type=int, default=500, metavar="MS", help="its time a move")
    args = ap.parse_args()

    go = f"go movetime {args.movetime}" if args.movetime else f"go nodes {args.nodes}"
    player = Player(args.opponent or args.host_engine, go, args.opt)
    judge = Player(args.eval, f"go movetime {args.eval_time}") if args.eval else None
    referee = Referee(args.host_engine)
    screen: Screen | BoardScreen
    if args.eth or args.port:
        link = ["--eth", args.eth] if args.eth else ["--port", args.port]
        screen = BoardScreen(args.nicemon, link, args.screen_bin)
    else:
        screen = Screen(args.emu, args.screen_bin)
    screen.press(*(["-"] * (3 - args.level) + ["+"] * (args.level - 3)))
    if args.black:
        screen.press("s", "r")  # The front end's engine plays white, and white is at the bottom
    cursor = (4, 1)  # e2, where the front end starts it
    moves: list[str] = []
    result = None
    try:
        while len(moves) < 2 * args.max_moves:
            side = ("white", "black")[len(moves) % 2]
            if (side == "black") == args.black:
                m = player.best(moves)
                # With the board turned, the arrows turn too, so black types.
                if args.black or len(moves) % 4 == 0 or len(m) == 5:
                    screen.press(*m, "\n")
                else:
                    frm, to = square(m[:2]), square(m[2:4])
                    screen.press(*arrows(cursor, frm), "\n", *arrows(frm, to), "\n")
                    cursor = to
                line = screen.line()
                if line != f"move {m}":
                    sys.exit(f"screen_game.py: {side} played {m}, the front end says: {line}")
            else:
                line = screen.line()
                if not line.startswith("move "):
                    sys.exit(f"screen_game.py: the front end says: {line}")
                m = line.split()[1]
            moves.append(m)
            result = referee.move(m)
            if result == "illegal":
                sys.exit(f"screen_game.py: {side}'s move {m} is illegal")
            if judge:
                number = f"{(len(moves) + 1) // 2:3d}." + ("" if side == "white" else "..")
                score = result or judge.score(moves)
                who = "nice_chess" if (side == "black") != args.black else "opponent"
                print(f"{number:6} {m:6} {who:11} {score}", flush=True)
            elif side == "black":
                print(f"{len(moves) // 2:3d}. {moves[-2]} {moves[-1]}", flush=True)
            if result:
                break
    finally:
        with open(args.keys_out, "w", encoding="latin-1", newline="") as f:
            f.write("".join(screen.keys))
        ended = screen.end(args.dump)
        player.send("quit")
        if judge:
            judge.send("quit")
        referee.close()
    print(" ".join(moves))
    print(result or f"no result after {args.max_moves} moves")
    if not ended:
        sys.exit("screen_game.py: the front end did not end normally")


if __name__ == "__main__":
    main()
