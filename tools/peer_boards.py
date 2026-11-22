#!/usr/bin/env python3
"""peer_boards.py PEER: a test of the peer (src/ui/peer.c) against two
boards in turn (#31).

The host build of the peer, PEER, waits at 127.0.0.2. Two boards, X at
127.0.0.3 and Y at 127.0.0.4, are played by this script: each sends a
message again every 500 ms until its ACK comes, and acknowledges the
peer's messages. X asks for a game and resigns; Y asks for one and
leaves it with BYE; X asks again and resigns. Each board must get its
game, and the peer's lines of game, result, and bye must be
tests/peer_boards.expected. Prints PASS or FAIL, and exits with 1 on a
failure. Only the Python standard library is used.
"""

import os
import socket
import subprocess
import sys
import time

PORT = 4511
PEER_IP, X_IP, Y_IP = "127.0.0.2", "127.0.0.3", "127.0.0.4"
LINES = ("game", "result", "bye", "move", "error", "gone")


class Board:
    """A board played by the script, at ip."""

    def __init__(self, ip: str) -> None:
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind((ip, PORT))
        self.s.settimeout(0.1)
        self.seq = 0

    def recv(self) -> str | None:
        """The next message from the peer, acknowledged if it has an S."""
        try:
            data, (ip, _) = self.s.recvfrom(2048)
        except socket.timeout:
            return None
        if ip != PEER_IP:
            return None
        m = data.decode()
        w = m.split(" ")
        if w[0] not in ("ACK", "HELLO?", "HELLO!") and len(w) > 1:
            self.s.sendto(f"ACK {w[1]}".encode(), (PEER_IP, PORT))
        return m

    def send(self, word: str, args: str = "", then: str | None = None) -> bool:
        """Sends a message until its ACK comes, and then waits for a
        message that starts with then. False if either does not come
        in 5 seconds."""
        self.seq += 1
        m = f"{word} {self.seq}" + (f" {args}" if args else "")
        end = time.monotonic() + 5
        acked, sent_at = False, 0.0
        while time.monotonic() < end:
            if not acked and time.monotonic() - sent_at >= 0.5:
                self.s.sendto(m.encode(), (PEER_IP, PORT))
                sent_at = time.monotonic()
            r = self.recv()
            if r is None:
                continue
            if r == f"ACK {self.seq}":
                acked = True
            elif then and r.startswith(then + " "):
                then = None
            if acked and not then:
                return True
        return False


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    x, y = Board(X_IP), Board(Y_IP)
    env = dict(os.environ, NICE_CHESS_IP=PEER_IP)
    peer = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            env=env, text=True)
    peer.stdout.readline()  # Its name: its network is open
    problems = []
    steps = [
        ("X's first game", x, "NEW", "w none", "ACCEPT"),
        ("X's RESIGN", x, "RESIGN", "", None),
        ("Y's game", y, "NEW", "w none", "ACCEPT"),
        ("Y's BYE", y, "BYE", "", None),
        ("X's second game", x, "NEW", "w none", "ACCEPT"),
        ("X's second RESIGN", x, "RESIGN", "", None),
    ]
    for what, board, word, args, then in steps:
        if not board.send(word, args, then):
            problems.append(f"{what}: no answer")
            break
    try:
        out, _ = peer.communicate("quit\n", timeout=10)
    except subprocess.TimeoutExpired:
        peer.kill()
        out, _ = peer.communicate()
    got = [line for line in out.splitlines() if line.split(" ")[0] in LINES]
    os.makedirs("build/test", exist_ok=True)
    with open("build/test/peer_boards.out", "w") as f:
        f.write("\n".join(got) + "\n")
    with open("tests/peer_boards.expected") as f:
        expected = f.read().splitlines()
    if peer.returncode != 0:
        problems.append(f"exit {peer.returncode}")
    if got != expected:
        problems.append("the peer's lines differ from tests/peer_boards.expected")
    if problems:
        print("FAIL peer_boards: " + "; ".join(problems))
        sys.exit(1)
    print("PASS peer_boards")


if __name__ == "__main__":
    main()
