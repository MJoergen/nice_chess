#!/usr/bin/env python3
"""board_remote.py - remote play (#27) between two boards on one network.

Usage: board_remote.py [--nicemon DIR] --eth IP --eth2 IP2 [--go LIMITS]
       [--nodes2 N] [--peer BIN] [--text BIN] [--referee CMD] [--pgn FILE]

Loads the player whose engine plays (src/ui/peer.c, --peer, the
Nexys4DDR's build by default) into the board at IP2, and the text front
end (--text, the MEGA65's build) into the board at IP, through their
debug links over Ethernet. The text front end connects to IP2 with
remote, asks for a game with new w, and plays white by remote auto,
with the limits of go (--go, movetime 2000 by default); the peer plays
black, at --nodes2 nodes a move, or its own default.

Both print the moves of the game, and its result: they must be the same
on both boards. The host's text front end (--referee) replays the moves,
each of which must be legal, and the game goes to --pgn as PGN. Exits
with 1 if the logs differ, a move is not legal, a board reports an
error, or the game does not end within --timeout seconds.

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import Board  # noqa: E402
from match import Referee  # noqa: E402
from pgn import Pgn  # noqa: E402

WORDS = ("game", "move", "result", "error", "bye", "gone", "back", "hello", "No network")


def clean(line: str) -> str:
    line = line.rstrip("\r")
    while line.startswith("> "):
        line = line[2:]
    return line


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--nicemon", help="a checkout of NiceMonMEGA65 (default $NICEMON)")
    ap.add_argument("--eth", required=True, help="the board of the text front end, white")
    ap.add_argument("--eth2", required=True, help="the board of the peer, black")
    ap.add_argument("--go", default="movetime 2000", help="the limits of white's go")
    ap.add_argument("--nodes2", type=int, help="the nodes of black's search")
    ap.add_argument("--peer", default="build/nexys/chess_peer.bin")
    ap.add_argument("--text", default="build/mega65/chess_text.bin")
    ap.add_argument("--referee", default="build/host/chess_text")
    ap.add_argument("--pgn", help="append the game to this file")
    ap.add_argument("--timeout", type=float, default=1800, help="seconds (default 1800)")
    args = ap.parse_args()

    peer = Board(args.nicemon, ["--eth", args.eth2], args.peer)
    if args.nodes2:
        peer.write(f"nodes {args.nodes2}\n".encode())
    # The peer is up once it has printed its banner.
    t = time.time()
    while time.time() - t < 30:
        line = peer.readline(timeout=1)
        if line and clean(line).startswith("nice_chess"):
            break
    text = Board(args.nicemon, ["--eth", args.eth], args.text)
    text.write(f"remote {args.eth2}\nnew w\nremote auto {args.go}\n".encode())

    logs: dict[str, list[str]] = {"white": [], "black": []}
    boards = {"white": text, "black": peer}
    done: set[str] = set()
    t0 = time.time()
    while len(done) < 2 and time.time() - t0 < args.timeout:
        for side, b in boards.items():
            if side in done:
                continue
            line = b.readline(timeout=0.05)
            if line is None:
                continue
            line = clean(line)
            if not line.startswith(WORDS):
                continue
            logs[side].append(line)
            print(f"{side:5}  {line}", flush=True)
            if line.startswith(("result", "error", "bye")):
                done.add(side)
    seconds = time.time() - t0
    for b in boards.values():
        b.end_input()
        b.wait_returned(30)
        b.close()

    problems = []
    if len(done) < 2:
        problems.append(f"no result within {args.timeout:.0f} seconds")
    moves = {s: [ln.split()[1] for ln in log if ln.startswith("move ")] for s, log in logs.items()}
    results = {s: [ln for ln in log if ln.startswith("result")] for s, log in logs.items()}
    if moves["white"] != moves["black"]:
        problems.append("the boards' moves differ")
    if results["white"] != results["black"]:
        problems.append("the boards' results differ")
    for s, log in logs.items():
        for ln in log:
            if ln.startswith(("error", "bye", "No network")):
                problems.append(f"{s}: {ln}")

    # The referee replays white's log, for the legality and the PGN.
    referee = Referee(args.referee)
    sans: list[str] = []
    for m in moves["white"]:
        if referee.play(m, sans) == "illegal":
            problems.append(f"an illegal move: {m}")
            break
    referee.close()
    result_line = results["white"][0] if results["white"] else "result *"
    words = result_line.split()
    result = words[1] if len(words) > 1 else "*"
    pgn = Pgn(args.pgn, "board_remote.py", "MEGA65 and Nexys4DDR")
    pgn.write(f"nice_chess text, {args.eth} (go {args.go})",
              f"nice_chess peer, {args.eth2}" + (f" (nodes {args.nodes2})" if args.nodes2 else ""),
              result if result in ("1-0", "0-1", "1/2-1/2") else "*", sans,
              why=" ".join(words[2:]))

    print(f"{len(moves['white'])} plies, {result_line[7:] or 'no result'}, in {seconds:.0f} seconds")
    if problems:
        print("FAIL board_remote: " + "; ".join(problems))
        sys.exit(1)
    print("PASS board_remote")


if __name__ == "__main__":
    main()
