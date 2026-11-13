#!/usr/bin/env python3
"""screen_uci.py - the windowed front end, as a UCI engine of the host.

Usage: screen_uci.py [--nicemon DIR] [--eth HOST | --port DEVICE]
                     [--emu NICEEMU] [--text TEXT.bin] [--debug] SCREEN.bin

Runs SCREEN.bin, the windowed front end, and is then a UCI engine that
plays it through its keys, as a player at its keyboard does. So the
screen shows the game that cutechess-cli, a chess GUI, or lichess-bot
plays against it. With --eth HOST or --port DEVICE, SCREEN.bin
(build/mega65/chess_screen.bin, built for the monitor) is loaded into
the MEGA65 through board.py. Without them it runs in niceemu
($NICEEMU, or --emu), with no window (build/nice/chess_screen.bin).

go types the opponent's move, as "e2e4" and Enter. The front end
writes each move to its console as "move e2e4", which is how this
script reads the engine's reply, its bestmove. For the engine to play
white, the script presses S. The opponent's side is at the bottom of
the screen, as the player's is.

The front end has no time control here: its clocks count up the time
each side has taken, and its engine plays at a level, the option Level
(1 to 5, 5 at first). The level sets the most time of a move, 45
seconds at level 5, and below level 5 the noise in the evaluation
(README.md). The time of a move is from go's clock, as the text front
end counts it at the most: a thirtieth of the side's time and three
quarters of its increment, but no more than half of its time; or it is
movetime. When it is up, the script presses Esc, which makes the
engine move now. stop presses Esc too, and so does quit during a
search with no such time; with one, quit waits for the move. In niceemu the front end does
not read the keys while it thinks, and its clock counts steps, so
there a move takes the time of the level.

The engine ponders, as the front end does by itself: it thinks on the
opponent's time, about the move it expects. UCI's go ponder is not
taken. The option OwnBook, false at first, is the front end's opening
book, its key O.

A position other than the one on the front end's board with one move
more is set up from a new game: the program is loaded again, and the
moves are made through the keys. The keys make the moves of one side
only, so for each move after the first the script presses S while the
engine thinks: the engine then moves for both sides, hurried by Esc,
and U takes the two moves back, which leaves the side to move to the
keys. This is at level 1. Pondering stays on: the front end's U does
not forget the move the engine expected, which it would then ponder
on, in another position, once P turned pondering on again. So a game
goes on from where it was when the script starts again, and a game
can start from an opening.

--text TEXT.bin names the text front end (build/mega65/chess_text.bin,
or build/nice/chess_text.bin), which plays on when the windowed one
cannot: when it does not answer as it should, as after a key pressed
on the MEGA65's keyboard, when it has ended a game that the host has
not, or when the position is not from the start position. The script
then loads the text front end, and passes UCI through to it for the
rest of the run, as board_uci.py does; the screen no longer shows the
game. Level is then the text front end's Noise. Without --text, the
script exits with 1.

On quit, or the end of stdin, the script leaves the windowed front end
running on the board, so that the screen keeps the game: once a
program has ended, the monitor writes its lines over the screen, in
its own font. Q on the MEGA65's keyboard ends the front end, and the
next load does not need that. In niceemu the script ends the program's
input, which ends the front end, and it sends the text front end quit;
it waits for either for up to 10 seconds. What the load prints goes to
stderr, and with --debug the host's lines, the keys pressed, the front
end's moves and results, and in niceemu the position it ends with.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import select
import subprocess
import sys
import time

from board import Board

ESC = "\x1b"
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
# The noise of the levels, in centipawns, as in src/ui/screen.c.
LEVEL_NOISE = [800, 600, 320, 170, 0]


class Lost(Exception):
    """The windowed front end is not where the game is."""


class Emu:
    """A program in niceemu, as a console, like board.py's Board."""

    def __init__(self, emu: str, binfile: str):
        self.p = subprocess.Popen([emu, "--step-time", binfile], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, bufsize=0)
        self.buf = bytearray()
        self.returned = False  # The program has ended

    def write(self, data: bytes) -> None:
        with contextlib.suppress(OSError, ValueError):
            self.p.stdin.write(data)

    def readline(self, timeout: float) -> str | None:
        end = time.monotonic() + timeout
        while True:
            i = self.buf.find(b"\n")
            if i >= 0:
                line = self.buf[:i].decode("latin-1").rstrip("\r")
                del self.buf[:i + 1]
                return line
            if self.returned:
                return None
            wait = max(0.0, end - time.monotonic())
            if not select.select([self.p.stdout], [], [], wait)[0]:
                return None
            data = os.read(self.p.stdout.fileno(), 65536)
            if data:
                self.buf += data
            else:
                self.returned = True

    def end_input(self) -> None:
        with contextlib.suppress(OSError, ValueError):
            self.p.stdin.close()

    def wait_returned(self, timeout: float) -> list[str]:
        lines = []
        end = time.monotonic() + timeout
        while not self.returned and time.monotonic() < end:
            line = self.readline(end - time.monotonic())
            if line is not None:
                lines.append(line)
        return lines

    def close(self) -> None:
        if self.p.poll() is None:
            self.p.kill()
        self.p.wait()


def budget(words: list[str], white: bool) -> float | None:
    """The time of the move that the words of go give, in seconds, as
    the text front end's do_go counts its hard limit, or None."""
    v = {}
    for name, value in zip(words, words[1:]):
        if name in ("movetime", "wtime", "btime", "winc", "binc", "movestogo") \
                and value.lstrip("-").isdigit():
            v[name] = max(0, int(value))
    if v.get("movetime"):
        return v["movetime"] / 1000
    side = "w" if white else "b"
    if side + "time" not in v:
        return None
    left, inc = v[side + "time"], v.get(side + "inc", 0)
    share = left // (v.get("movestogo") or 30) + inc * 3 // 4  # engine_time_share
    return max(min(share, left // 2), 1) / 1000


class Engine:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        # On a board the front end reads the keys while it thinks.
        self.live = bool(args.eth or args.port)
        # How long the engine may take by itself, level 5's 45 seconds,
        # and how long the front end may take to write a typed move.
        self.patience = 60 if self.live else 900
        self.echo = 5 if self.live else 20
        self.level = 5
        self.book = False
        self.con: Board | Emu | None = None
        self.text = False           # The text front end runs, and UCI passes through
        self.front: list[str] = []  # The moves on the windowed front end's board
        self.human = 0              # The side the keys play: 0 white, 1 black
        self.pristine = False       # No key has been pressed since the load
        self.new = False            # ucinewgame came since the last go
        self.position = "position startpos"
        self.moves: list[str] | None = []  # Of position, or None if not from the start
        self.stdin = b""
        self.queue: list[str] = []
        self.stop = self.quit = False
        self.load(args.bin)

    def load(self, binfile: str) -> None:
        if self.con:
            self.con.close()
        if self.live:
            link = ["--eth", self.args.eth] if self.args.eth else ["--port", self.args.port]
            # stdout is the UCI stream, so nothing else may go there.
            with contextlib.redirect_stdout(sys.stderr):
                self.con = Board(self.args.nicemon, link, binfile)
        else:
            self.con = Emu(self.args.emu, binfile)
        self.front, self.human, self.pristine = [], 0, True

    # The host's lines

    def commands(self, timeout: float, searching: bool = False) -> None:
        """Reads the lines of stdin that wait into the queue. During a
        search of the windowed front end, isready, stop, and quit act
        at once."""
        if self.quit or not select.select([0], [], [], timeout)[0]:
            return
        self.stdin += os.read(0, 4096) or b"\nquit\n"
        while b"\n" in self.stdin:
            raw, self.stdin = self.stdin.split(b"\n", 1)
            line = raw.decode("latin-1").strip()
            if not line:
                continue
            if not searching or line not in ("isready", "stop", "quit"):
                self.queue.append(line)
            elif line == "isready":
                print("readyok", flush=True)
            elif line == "stop":
                self.stop = True
            else:
                self.quit = True
            if line == "quit":
                self.stdin = b""
                break

    def command(self, line: str) -> None:
        if self.text:
            self.send(line)
            self.quit = line == "quit"
            return
        words = line.split()
        if self.args.debug:
            print(f"uci   {line}", file=sys.stderr, flush=True)
        if words[:1] == ["quit"]:
            self.quit = True
        elif words[:1] == ["isready"]:
            print("readyok", flush=True)
        elif words[:1] == ["uci"]:
            print("id name nice_chess, the windowed front end\n"
                  "id author Michael Jorgensen\n"
                  "option name OwnBook type check default false\n"
                  "option name Level type spin default 5 min 1 max 5\nuciok", flush=True)
        elif words[:1] == ["setoption"] and "name" in words and "value" in words:
            name = " ".join(words[words.index("name") + 1:words.index("value")]).lower()
            value = " ".join(words[words.index("value") + 1:]).lower()
            if name == "ownbook":
                self.book = value == "true"
            elif name == "level" and value.isdigit():
                self.level = min(max(int(value), 1), 5)
        elif words[:1] == ["ucinewgame"]:
            self.new = True
        elif words[:1] == ["position"]:
            self.position = line
            at = words.index("moves") if "moves" in words else len(words)
            start = words[1:at] == ["startpos"] or words[1:at] == ["fen"] + START_FEN.split()
            self.moves = words[at + 1:] if start else None
        elif words[:1] == ["go"]:
            self.go(line)

    # The windowed front end

    def press(self, keys: str) -> None:
        if keys:
            if self.args.debug:
                print(f"keys  {keys!r}", file=sys.stderr, flush=True)
            self.pristine = False
            self.con.write(keys.encode("latin-1"))

    def line(self, timeout: float) -> str | None:
        """The next move or result on the front end's console, or None
        at the timeout. The host's lines are read meanwhile."""
        end = time.monotonic() + timeout
        while True:
            self.commands(0, True)
            line = self.con.readline(0.01)
            if line is not None:
                if line.startswith(("move ", "result ")):
                    if self.args.debug:
                        print(f"front {line}", file=sys.stderr, flush=True)
                    return line
            elif self.con.returned:
                raise Lost("the windowed front end has ended")
            elif time.monotonic() >= end:
                return None

    def enter(self, m: str) -> None:
        """Types a move of the side the keys play, and reads it back. A
        result before it is of a move the engine was hurried to."""
        self.press(m + "\n")
        line = self.line(self.echo)
        while line and line.startswith("result "):
            line = self.line(self.echo)
        if line != f"move {m}":
            raise Lost(f"the windowed front end did not take {m}: {line or 'no answer'}")
        self.front.append(m)

    def hurried(self) -> str:
        """The next move or result, with Esc for the engine to move now."""
        end = time.monotonic() + self.patience
        while time.monotonic() < end:
            if self.live:
                self.press(ESC)
            line = self.line(0.3 if self.live else self.patience)
            if line:
                return line
        raise Lost("the windowed front end does not move")

    def switch(self) -> None:
        """Gives the keys the side to move, about whose move the engine
        thinks. S waits for that move, and has the engine play the other
        side, so it moves again. U takes back both."""
        self.press("s")
        line = self.hurried()
        if line.startswith("result "):
            raise Lost(f"the windowed front end has ended the game: {line[7:]}")
        self.hurried()  # A move, or the result of the first
        self.press("u")
        self.human ^= 1

    def setup(self, moves: list[str]) -> None:
        """Makes the moves from a new game, and leaves the engine to
        think about its move after them."""
        if not self.pristine or self.con.returned:
            self.load(self.args.bin)
        book = "" if self.book else "o"
        level = "+" * (self.level - 1)
        if not moves:
            self.press(book + level + "s")
            self.human = 1
            return
        self.press(book)
        for i, m in enumerate(moves):
            if i:
                self.switch()
            if i == len(moves) - 1:
                self.press(level)
            self.enter(m)

    def best(self, start: float, limit: float | None) -> str:
        """The engine's move. Esc at the limit after start, or at stop,
        makes it move now, on a board. quit waits for a move that has a
        limit, as the text front end's does, and is a stop without one."""
        pressed = None
        while True:
            now = time.monotonic()
            late = self.stop or (now - start >= limit if limit is not None else self.quit)
            if late and self.live and (pressed is None or now - pressed >= 1):
                self.press(ESC)
                pressed = now
            if now - start > (limit or 0) + self.patience:
                raise Lost("the windowed front end does not move")
            wait = 0.05 if late or limit is None else min(0.05, start + limit - now)
            line = self.line(max(wait, 0.001))
            if line and line.startswith("result "):
                raise Lost(f"the windowed front end has ended the game: {line[7:]}")
            if line:
                self.front.append(line.split()[1])
                return self.front[-1]

    def play(self, limit: float | None) -> str:
        start = time.monotonic()
        moves = self.moves
        n = len(moves)
        on_board = not (self.new or self.pristine or self.con.returned)
        if on_board and self.front[:-1] == moves and len(self.front) == n + 1 \
                and n % 2 != self.human:
            return self.front[n]  # Asked again
        if on_board and n and self.front == moves[:-1] and n % 2 != self.human:
            self.enter(moves[-1])
        else:
            self.setup(moves)
            start = time.monotonic()  # The engine has its time from here
        self.new = False
        return self.best(start, limit)

    def go(self, line: str) -> None:
        self.stop = False
        try:
            if self.moves is None:
                raise Lost("the position is not from the start position")
            move = self.play(budget(line.split(), len(self.moves) % 2 == 0))
        except Lost as e:
            self.fall_back(str(e), line)
            return
        # bestmove of go infinite waits for stop.
        while "infinite" in line.split() and not (self.stop or self.quit):
            self.commands(0.05, True)
            self.idle()
        print(f"bestmove {move}", flush=True)

    def idle(self) -> None:
        """What the program prints between searches. A move of the
        windowed front end is of a key on the MEGA65's keyboard."""
        line = self.con.readline(0.01)
        if line is None:
            return
        if self.text:
            while line.startswith("> "):
                line = line[2:]
            if line and not line.startswith("Returned "):
                print(line, flush=True)
        elif line.startswith("move "):
            self.front.append(line.split()[1])

    # The text front end

    def send(self, line: str) -> None:
        self.con.write((line + "\n").encode("latin-1"))

    def fall_back(self, why: str, go: str) -> None:
        """Loads the text front end, and sends it the position and go."""
        print(f"screen_uci.py: {why}", file=sys.stderr, flush=True)
        if not self.args.text:
            self.con.close()
            sys.exit(1)
        print(f"info string {why}: the text front end plays on", flush=True)
        self.load(self.args.text)
        self.text = True
        self.send("uci")
        end = time.monotonic() + 30
        while (self.con.readline(0.5) or "").split()[-1:] != ["uciok"]:
            if self.con.returned or time.monotonic() > end:
                self.con.close()
                sys.exit("screen_uci.py: the text front end does not answer uci")
        self.send(f"setoption name OwnBook value {'true' if self.book else 'false'}")
        self.send(f"setoption name Noise value {LEVEL_NOISE[self.level - 1]}")
        self.send(self.position)
        self.send(go)

    def run(self) -> int:
        while not self.quit:
            self.commands(0 if self.text else 0.01)
            while self.queue and not self.quit:
                self.command(self.queue.pop(0))
            if not self.quit:
                self.idle()
            if self.text and self.con.returned:
                break
        if self.text or not self.live:
            if not self.text:
                self.con.end_input()
            for line in self.con.wait_returned(10):
                if self.args.debug and line.startswith("fen "):
                    print(f"front {line}", file=sys.stderr, flush=True)
            if not self.con.returned:
                print("screen_uci.py: the program did not end", file=sys.stderr)
        self.con.close()
        return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--nicemon", help="a checkout of NiceMonMEGA65 (default $NICEMON)")
    ap.add_argument("--eth", metavar="HOST[:PORT]", help="the board's Ethernet port")
    ap.add_argument("--port", metavar="DEVICE", help="the board's debug UART")
    ap.add_argument("--emu", default=os.environ.get("NICEEMU", "niceemu"),
                    help="niceemu, with neither --eth nor --port")
    ap.add_argument("--text", metavar="TEXT.bin",
                    help="the text front end, to play on with if the windowed one cannot")
    ap.add_argument("--debug", action="store_true",
                    help="print the host's lines, the keys, and the front end's moves to stderr")
    ap.add_argument("bin")
    return Engine(ap.parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
