#!/usr/bin/env python3
"""remote_test.py NAME CHECK PEER [--text TEXT] [--relay]: a test of
remote play (#27) on the host.

Two host builds of the peer (src/ui/peer.c), PEER, play each other over
the loopback: B at 127.0.0.2, which waits, and A at 127.0.0.1, which
sends HELLO? and NEW, and plays white. Both search 2000 nodes a move,
with seeds of their own for the opening book, so the game is the same
every time. Both bind UDP port 4511, at their own addresses.

With --text, A is the host build of the text front end, TEXT, which
connects with remote, asks with new w, and plays by remote auto, at
2000 nodes a move, without the book.

With --relay, A plays through a relay at 127.0.0.3, which passes each
datagram on to the other side, but drops every fifth of each direction
and sends every seventh twice. The resends and the ACKs must make the
game the same as without it.

The moves and the result of A and of B must be the same, and A's lines
of hello, game, move, result, error, and bye must be
tests/CHECK.expected, after the prompts "> " are taken off. Prints PASS or FAIL, and exits with 1 on a
failure. Only the Python standard library is used.
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import threading

PORT = 4511
A_IP, B_IP, RELAY_IP = "127.0.0.1", "127.0.0.2", "127.0.0.3"
TIMEOUT = 120
LINES = ("hello", "game", "move", "result", "error", "bye")


def relay(stop: threading.Event, counts: dict) -> None:
    """Passes the datagrams between A and B, dropping and doubling some."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((RELAY_IP, PORT))
    s.settimeout(0.1)
    n = {A_IP: 0, B_IP: 0}
    while not stop.is_set():
        try:
            data, (ip, _) = s.recvfrom(2048)
        except socket.timeout:
            continue
        if ip not in n:
            continue
        to = B_IP if ip == A_IP else A_IP
        if data.startswith(b"HELLO"):
            s.sendto(data, (to, PORT))
            continue
        n[ip] += 1
        if n[ip] % 5 == 0:
            counts["dropped"] += 1
            continue
        s.sendto(data, (to, PORT))
        if n[ip] % 7 == 0:
            counts["doubled"] += 1
            s.sendto(data, (to, PORT))
    s.close()


def run(peer: str, ip: str, commands: str, path: str) -> subprocess.Popen:
    """Starts the peer at ip, with the commands as its console input,
    from the file at path."""
    with open(path, "w") as f:
        f.write(commands)
    env = dict(os.environ, NICE_CHESS_IP=ip)
    with open(path) as f:
        return subprocess.Popen([peer], stdin=f, stdout=subprocess.PIPE, env=env, text=True)


def lines(out: str) -> list[str]:
    out = re.sub(r"^(> )+", "", out, flags=re.M)
    return [line for line in out.splitlines() if line.split(" ")[0] in LINES]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("check")
    ap.add_argument("peer")
    ap.add_argument("--text")
    ap.add_argument("--relay", action="store_true")
    args = ap.parse_args()
    name, peer, use_relay = args.name, args.peer, args.relay
    stop = threading.Event()
    counts = {"dropped": 0, "doubled": 0}
    t = None
    if use_relay:
        t = threading.Thread(target=relay, args=(stop, counts))
        t.start()
    other = RELAY_IP if use_relay else B_IP
    os.makedirs("build/test", exist_ok=True)
    b = run(peer, B_IP, "seed 2\nnodes 2000\n", f"build/test/{name}_b.input")
    if args.text:
        a = run(args.text, A_IP, f"remote {other}\nnew w\nremote auto nodes 2000\n",
                f"build/test/{name}_a.input")
    else:
        a = run(peer, A_IP, f"seed 1\nnodes 2000\nhello {other}\nconnect {other}\nnew w\n",
                f"build/test/{name}_a.input")
    try:
        out_a, _ = a.communicate(timeout=TIMEOUT)
        out_b, _ = b.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        a.kill()
        b.kill()
        out_a, _ = a.communicate()
        out_b, _ = b.communicate()
    finally:
        stop.set()
        if t:
            t.join()

    got = lines(out_a)
    with open(f"build/test/{name}.out", "w") as f:
        f.write("\n".join(got) + "\n")
    with open(f"tests/{args.check}.expected") as f:
        expected = f.read().splitlines()
    # In A's log the hello names the relay; B is 127.0.0.2 behind it.
    got_cmp = [line.replace(RELAY_IP, B_IP) for line in got]

    def moves(ls: list[str]) -> list[str]:
        return [line for line in ls if line.startswith(("move", "result"))]

    problems = []
    if a.returncode != 0 or b.returncode != 0:
        problems.append(f"exit A {a.returncode}, B {b.returncode}")
    if moves(got) != moves(lines(out_b)):
        problems.append("A and B differ in the moves or the result")
    if got_cmp != expected:
        problems.append(f"A's lines differ from tests/{args.check}.expected")
    if use_relay and not counts["dropped"]:
        problems.append("the relay dropped nothing")
    extra = f" ({counts['dropped']} dropped, {counts['doubled']} doubled)" if use_relay else ""
    if problems:
        print(f"FAIL {name}{extra}: " + "; ".join(problems))
        sys.exit(1)
    print(f"PASS {name}{extra}")


if __name__ == "__main__":
    main()
