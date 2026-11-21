#!/usr/bin/env python3
"""remote_talk.py NAME SCRIPT TEXT: a test of remote play (#27) between
two text front ends on the host.

Two host builds of the text front end, TEXT, run at 127.0.0.1 (A) and
127.0.0.2 (B), both at UDP port 4511, with their consoles on pipes.
SCRIPT is the talk, a step on each line:

  A> LINE     types LINE at A's console
  A< TEXT     waits until A prints a line that starts with TEXT, after
              its prompts "> " are taken off, for at most 10 seconds;
              other lines before it are passed over
  A!          kills A, as a board that is switched off
  A.          waits until A has ended by itself, for at most --end seconds

and the same with B. Empty lines and lines that start with # are left
out. After the last step the input of both ends, and both must exit
with 0 within 15 seconds. What the two print goes to
build/test/NAME.out. Prints PASS or FAIL, and exits with 1 on a
failure. Only the Python standard library is used.
"""

import argparse
import os
import queue
import re
import subprocess
import sys
import threading
import time

IPS = {"A": "127.0.0.1", "B": "127.0.0.2"}
WAIT = 10
EXIT = 15


def reader(p: subprocess.Popen, q: queue.Queue) -> None:
    for line in p.stdout:
        q.put(line)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("script")
    ap.add_argument("text")
    ap.add_argument("--end", type=float, default=EXIT,
                    help="seconds a step 'A.' waits for the end (default 15)")
    args = ap.parse_args()

    procs: dict[str, subprocess.Popen] = {}
    lines: dict[str, queue.Queue] = {}
    for who, ip in IPS.items():
        procs[who] = subprocess.Popen([args.text], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      env=dict(os.environ, NICE_CHESS_IP=ip), text=True, bufsize=1)
        lines[who] = queue.Queue()
        threading.Thread(target=reader, args=(procs[who], lines[who]), daemon=True).start()

    os.makedirs("build/test", exist_ok=True)
    log = open(f"build/test/{args.name}.out", "w")
    problem = ""
    killed = set()

    def expect(who: str, text: str) -> bool:
        end = time.time() + WAIT
        while time.time() < end:
            try:
                line = lines[who].get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                break
            line = re.sub(r"^(> )+", "", line.rstrip("\n"))
            log.write(f"{who}  {line}\n")
            if line.startswith(text):
                return True
        return False

    with open(args.script) as f:
        for n, step in enumerate(f, 1):
            step = step.rstrip("\n")
            if not step.strip() or step.startswith("#"):
                continue
            who, what, text = step[0], step[1:2], step[3:]
            if who not in procs or what not in (">", "<", "!", "."):
                problem = f"{args.script}:{n}: not a step: {step}"
            elif what == ">":
                log.write(f"{who}> {text}\n")
                try:
                    procs[who].stdin.write(text + "\n")
                    procs[who].stdin.flush()
                except BrokenPipeError:
                    problem = f"{args.script}:{n}: {who} has ended"
            elif what == "<":
                if not expect(who, text):
                    problem = f"{args.script}:{n}: {who} did not print: {text}"
            elif what == "!":
                log.write(f"{who}!\n")
                procs[who].kill()
                procs[who].wait()
                killed.add(who)
            else:
                try:
                    procs[who].wait(timeout=args.end)
                    log.write(f"{who}. exit {procs[who].returncode}\n")
                except subprocess.TimeoutExpired:
                    problem = f"{args.script}:{n}: {who} did not end in {args.end:.0f} seconds"
            if problem:
                break

    for who, p in procs.items():
        if p.stdin and not p.stdin.closed:
            try:
                p.stdin.close()
            except BrokenPipeError:
                pass
    for who, p in procs.items():
        try:
            p.wait(timeout=EXIT)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            problem = problem or f"{who} did not end after the end of its input"
        if p.returncode != 0 and who not in killed:
            problem = problem or f"{who} exited with {p.returncode}"
    log.close()

    if problem:
        print(f"FAIL {args.name}: {problem}")
        sys.exit(1)
    print(f"PASS {args.name}")


if __name__ == "__main__":
    main()
