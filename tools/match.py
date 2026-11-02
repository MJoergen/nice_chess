#!/usr/bin/env python3
"""match.py - plays games between two UCI engines, or an engine and random moves.

Usage: match.py [options] REFEREE ENGINE1 ENGINE2

REFEREE is the host build of the text front end (build/host/chess_text),
which keeps the game: it checks each move, and says when the game has
ended. So no chess library is needed.

An ENGINE is a command line, such as "build/host/chess_text", or the
word "random", which plays a random legal move.

Each opening of OPENINGS is played twice, with each engine as white once.
The engines get a time control of --moves moves in --time seconds,
repeating, as wtime, btime, and movestogo. Only the time an engine takes
to answer go counts. A game of more than --max-moves moves is a draw.

--go1 and --go2 give an engine a go command of its own instead, such as
"go nodes 10000", or "go" for its default time; its time is then not
counted. That is how to play an engine in niceemu, whose clock counts
steps, not real time: "niceemu build/nice/chess_text.bin" with --go2 go.

Prints each game, and the score of ENGINE1. Exits with 1 if an engine
makes an illegal move or loses on time by more than --grace seconds, or
if ENGINE1 scores less than --min-score.
"""

from __future__ import annotations

import argparse
import random
import shlex
import subprocess
import sys
import time

OPENINGS = [
    "e2e4 e7e5", "d2d4 d7d5", "c2c4 e7e5", "e2e4 c7c5", "d2d4 g8f6",
    "g1f3 d7d5", "e2e4 e7e6", "e2e4 c7c6", "d2d4 f7f5", "b1c3 d7d5",
]

SYNC = "__sync__"


class Referee:
    """The text front end, as the keeper of the game."""

    def __init__(self, cmd: str):
        self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, bufsize=1)

    def ask(self, cmd: str) -> list[str]:
        """Sends a command, and returns the lines it printed."""
        self.p.stdin.write(f"{cmd}\n{SYNC}\n")
        self.p.stdin.flush()
        lines = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("the referee has quit")
            line = line.rstrip("\n")
            while line.startswith("> "):
                line = line[2:]
            if line.startswith("Unknown command: " + SYNC):
                return lines
            lines.append(line)

    def moves(self) -> list[str]:
        return self.ask("moves")[0].split()

    def move(self, m: str) -> str | None:
        """Makes a move. Returns None, or how the game ended, or "illegal"."""
        for line in self.ask(f"move {m}"):
            if line.startswith("Illegal") or line.startswith("The game is too long"):
                return "illegal"
            if line.startswith(("Checkmate", "Stalemate", "Draw")):
                return line
        return None

    def close(self):
        self.p.stdin.write("quit\n")
        self.p.stdin.close()
        self.p.wait()


class Engine:
    """A UCI engine, or random moves."""

    def __init__(self, cmd: str, rng: random.Random, go_cmd: str | None):
        self.name = cmd
        self.rng = rng
        self.go_cmd = go_cmd
        self.p = None
        if cmd != "random":
            self.p = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, text=True, bufsize=1)
            self.send("uci")
            self.wait_for("uciok")

    def send(self, s: str):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def wait_for(self, word: str) -> str:
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError(f"{self.name} has quit")
            if line.split()[:1] == [word]:
                return line

    def new_game(self):
        if self.p:
            self.send("ucinewgame")
            self.send("isready")
            self.wait_for("readyok")

    def go(self, moves: list[str], legal: list[str], wtime: int, btime: int, togo: int) -> str:
        if not self.p:
            return self.rng.choice(legal)
        self.send("position startpos moves" + "".join(" " + m for m in moves))
        self.send(self.go_cmd or f"go wtime {wtime} btime {btime} movestogo {togo}")
        return self.wait_for("bestmove").split()[1]

    def close(self):
        if self.p:
            self.send("quit")
            self.p.stdin.close()
            self.p.wait()


def play(ref: Referee, white: Engine, black: Engine, opening: str, args) -> tuple[str, str]:
    """Plays a game, and returns its result, "1-0", "0-1", or "1/2-1/2", and why."""
    ref.ask("new")
    moves = []
    for m in opening.split():
        ref.move(m)
        moves.append(m)
    for e in (white, black):
        e.new_game()

    clock = [args.time * 1000, args.time * 1000]  # White, black, in ms
    engines = [white, black]
    while True:
        side = len(moves) % 2
        if len(moves) >= 2 * args.max_moves:
            return "1/2-1/2", "too long"
        togo = args.moves - (len(moves) // 2) % args.moves
        legal = ref.moves()
        t0 = time.monotonic()
        m = engines[side].go(moves, legal, clock[0], clock[1], togo)
        if not engines[side].go_cmd:
            clock[side] -= int((time.monotonic() - t0) * 1000)
        if clock[side] < -1000 * args.grace:
            return ("0-1", "1-0")[side], f"{engines[side].name} lost on time"
        if (len(moves) // 2) % args.moves == args.moves - 1 and side == 1:
            clock[0] += args.time * 1000
            clock[1] += args.time * 1000
        end = ref.move(m)
        moves.append(m)
        if end == "illegal":
            return ("0-1", "1-0")[side], f"{engines[side].name} played {m}, which is illegal"
        if end and end.startswith("Checkmate"):
            return ("1-0", "0-1")[side], end
        if end:
            return "1/2-1/2", end


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("referee")
    ap.add_argument("engine1")
    ap.add_argument("engine2")
    ap.add_argument("--time", type=int, default=20, help="seconds per --moves moves")
    ap.add_argument("--moves", type=int, default=40)
    ap.add_argument("--max-moves", type=int, default=150)
    ap.add_argument("--grace", type=int, default=2, help="seconds over time before a loss")
    ap.add_argument("--openings", type=int, default=len(OPENINGS))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--min-score", type=float, default=0)
    ap.add_argument("--go1", help="the go command of ENGINE1, instead of the clock")
    ap.add_argument("--go2", help="the go command of ENGINE2, instead of the clock")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    ref = Referee(args.referee)
    e1 = Engine(args.engine1, rng, args.go1)
    e2 = Engine(args.engine2, rng, args.go2)
    score = 0.0
    games = 0
    bad = False
    for opening in OPENINGS[:args.openings]:
        for first in (e1, e2):
            white, black = (first, e2 if first is e1 else e1)
            result, why = play(ref, white, black, opening, args)
            games += 1
            points = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[result]
            score += points if white is e1 else 1.0 - points
            bad |= "illegal" in why or "on time" in why
            print(f"{games:3}. {white.name} - {black.name}: {result} ({why}), opening {opening}",
                  flush=True)
    print(f"{args.engine1}: {score:g} of {games}")
    for e in (e1, e2):
        e.close()
    ref.close()
    return 1 if bad or score < args.min_score else 0


if __name__ == "__main__":
    sys.exit(main())
