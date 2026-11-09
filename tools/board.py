#!/usr/bin/env python3
"""board.py - a program under the monitor on the MEGA65, as its console.

Usage: board.py [--nicemon DIR] LINK [-i INPUT] [-s | --color] [--dump FILE] PROG.bin

Loads PROG.bin, built with nicebuild.py --monitor, into the board
through the debug link of NiceMonMEGA65, types r to the monitor, which
runs the program, and is then the program's console: INPUT (a file, or
stdin) goes to the program, paced to its input buffer, and what it
prints comes to stdout. The run ends when the monitor prints Returned,
as the program has returned to it. If the program is still running once
the input has gone in and it has been quiet for --settle seconds, the
screen is read, and then a break ends the program's input, as the end
of niceemu's input does. With -s, the screen is printed at the end, as
niceemu --screen prints it, and with --color as niceemu --color does,
so that a run on the board can be compared with one in niceemu. --dump
FILE writes the screen's 2000 words, the font base, and the font RAM and
the character ROM to FILE as niceemu -d 0xFFFF0000:2000 -d
0xFFFFFF86:1 -d 0xFFFE0000:16384 prints them, for screenshot.py.

LINK is how to reach the board, as NiceMonMEGA65's tools take it:
--eth HOST for its Ethernet port, or --port DEVICE for its debug UART.
--nicemon DIR (or $NICEMON, or ../NiceMonMEGA65) is a checkout of
NiceMonMEGA65, whose tools/ has the link, in Python with no packages.

match.py and screen_game.py import the class Board here, to play the
program on the board as they play one in niceemu.
"""

from __future__ import annotations

import argparse
import importlib
import os
import sys
import time

SCREEN = 0xFFFF0000
SCREEN_WORDS = 2000
# The font base, the font RAM, and the character ROM (nice_cpu's SYSTEM.md §1.8)
FONT_RANGES = [(0xFFFFFF86, 1), (0xFFFE0000, 8192), (0xFFFE2000, 8192)]


# The module of NiceMonMEGA65's tools with the debug link: boardlink.py
# from its V1.9 on, nicelink.py before
LINK_MODULES = ("boardlink", "nicelink")


def tools_dir(nicemon: str | None) -> str:
    d = nicemon or os.environ.get("NICEMON") or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "..", "NiceMonMEGA65")
    tools = os.path.join(d, "tools")
    if not any(os.path.exists(os.path.join(tools, m + ".py")) for m in LINK_MODULES):
        sys.exit(f"board.py: {tools}/boardlink.py not found; --nicemon DIR or NICEMON=DIR "
                 "names a checkout of NiceMonMEGA65")
    return tools


class Board:
    """A program under the monitor on the board, as a console."""

    def __init__(self, nicemon: str | None, link_args: list[str], binfile: str):
        tools = tools_dir(nicemon)
        sys.path.insert(0, tools)
        import load as loader  # noqa: E402  NiceMonMEGA65/tools/load.py
        nicelink = importlib.import_module(next(
            m for m in LINK_MODULES if os.path.exists(os.path.join(tools, m + ".py"))))

        self.nicelink = nicelink
        ap = argparse.ArgumentParser(prog="board.py")
        nicelink.add_arguments(ap)
        args = ap.parse_args(link_args)
        self.link = nicelink.open_link(args)
        words = loader.monitor_words(binfile, None)
        loader.load_words(self.link, words, rom=True, monitor=True)
        loader.start(self.link, rom=True)
        self.fifo = self.link.free()  # The input buffer, empty after the load
        self.link.send_console(loader.RUN_COMMAND)
        self.buf = bytearray()
        self.returned = False  # The monitor has printed Returned
        self.last_rx = time.monotonic()  # When the program last printed

    def write(self, data: bytes) -> None:
        """Sends data to the program's input, paced to its buffer."""
        self.link.send_console(data)

    def readline(self, timeout: float | None = None) -> str | None:
        """The next line the program printed, without its end, or None
        at the timeout, or when the monitor has printed Returned."""
        end = None if timeout is None else time.monotonic() + timeout
        while True:
            i = self.buf.find(b"\n")
            if i >= 0:
                line = self.buf[:i].decode("latin-1").rstrip("\r")
                del self.buf[:i + 1]
                if line.startswith("Returned "):
                    self.returned = True
                return line
            if self.returned:
                return None
            wait = 0.5 if end is None else min(0.5, end - time.monotonic())
            if wait <= 0:
                return None
            self.link.poll(wait)
            data = self.link.take_console()
            if data:
                self.buf += data
                self.last_rx = time.monotonic()

    def quiet(self, seconds: float) -> bool:
        """Whether the program has read all of its input, and printed
        nothing for seconds."""
        return (time.monotonic() - self.last_rx >= seconds
                and self.link.free() >= self.fifo)

    def end_input(self) -> None:
        """Ends the program's input with a break, as the end of niceemu's
        input does."""
        self.link.end_input()

    def wait_returned(self, timeout: float) -> list[str]:
        """Reads lines until the monitor prints Returned, and returns
        them."""
        lines = []
        end = time.monotonic() + timeout
        while not self.returned:
            line = self.readline(max(0.0, end - time.monotonic()))
            if line is None:
                break
            lines.append(line)
        return lines

    def screen(self) -> list[int]:
        """The 2000 words of the screen. The core is paused for the
        read, and goes on afterwards."""
        nl = self.nicelink
        self.link.control(nl.PAUSE)
        words = self.link.read(SCREEN, SCREEN_WORDS)
        self.link.control(0)
        return words

    def font(self) -> list[tuple[int, list[int]]]:
        """The font base, the font RAM, and the character ROM, as
        (address, words). The core is paused for the read."""
        nl = self.nicelink
        self.link.control(nl.PAUSE)
        out = [(a, self.link.read(a, n)) for a, n in FONT_RANGES]
        self.link.control(0)
        return out

    def halt(self) -> None:
        """Halts the core through the monitor's h, once the program has
        returned to it."""
        self.link.send_console(b"h\r")

    def close(self) -> None:
        self.link.close()


def screen_lines(words: list[int]) -> list[str]:
    """The screen as niceemu --screen prints it."""
    chars = "".join(chr(w & 0xFF) if 0x20 < w & 0xFF < 0x7F else " " for w in words)
    rows = [chars[y * 80:(y + 1) * 80].rstrip() for y in range(25)]
    while rows and not rows[-1]:
        rows.pop()
    return ["Screen:"] + [f"|{row}" for row in rows]


def rgb(c: int) -> tuple[int, int, int]:
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def color_lines(words: list[int]) -> list[str]:
    """The screen as niceemu --color prints it: each row with the colors
    of its words as 24-bit ANSI escapes, where they change, and a reset
    at its end."""
    rows = []
    last = 0
    for y in range(25):
        row = words[y * 80:(y + 1) * 80]
        chars = "".join(chr(w & 0xFF) if 0x20 < w & 0xFF < 0x7F else " " for w in row)
        if chars.strip():
            last = y + 1
        rows.append((chars.rstrip(" "), row))
    out = ["Screen:"]
    for text, row in rows[:last]:
        prev = None
        line = "|"
        for ch, w in zip(text, row):
            now = (rgb(w >> 20), rgb(w >> 8))
            if now != prev:
                line += "\x1b[48;2;%d;%d;%d;38;2;%d;%d;%dm" % (now[0] + now[1])
                prev = now
            line += ch
        out.append(line + "\x1b[0m")
    return out


def dump_lines(addr: int, words: list[int]) -> list[str]:
    """Words from addr, as niceemu -d prints them."""
    return [f"{addr + i:08x}:" + "".join(f" {w:08x}" for w in words[i:i + 8])
            for i in range(0, len(words), 8)]


def main() -> None:
    ap = argparse.ArgumentParser(description="A program under the monitor on the board.")
    ap.add_argument("--nicemon", help="a checkout of NiceMonMEGA65 (default $NICEMON)")
    ap.add_argument("--eth", metavar="HOST[:PORT]", help="the board's Ethernet port")
    ap.add_argument("--port", metavar="DEVICE", help="the board's debug UART")
    ap.add_argument("-i", "--input", help="the program's input (default stdin)")
    ap.add_argument("-s", "--screen", action="store_true", help="print the screen at the end")
    ap.add_argument("--color", action="store_true", help="print it with its colors")
    ap.add_argument("--dump", metavar="FILE", help="write the screen's and the font's words to FILE at the end")
    ap.add_argument("--settle", type=float, default=2,
                    help="seconds of quiet before the screen is read and the input ended (default 2)")
    ap.add_argument("--timeout", type=float, default=600, help="seconds (default 600)")
    ap.add_argument("bin")
    args = ap.parse_args()

    link_args = ["--eth", args.eth] if args.eth else ["--port", args.port] if args.port else []
    if not link_args:
        ap.error("--eth HOST or --port DEVICE is needed")
    with open(args.input, "rb") if args.input else sys.stdin.buffer as f:
        data = f.read()
    board = Board(args.nicemon, link_args, args.bin)
    end = time.monotonic() + args.timeout
    board.write(data)
    words = None
    font: list[tuple[int, list[int]]] = []
    while not board.returned and time.monotonic() < end:
        line = board.readline(min(0.5, end - time.monotonic()))
        if line is not None:
            print(line, flush=True)
        elif words is None and board.quiet(args.settle):
            words = board.screen()
            if args.dump:
                font = board.font()
            board.end_input()
    if words is None:
        words = board.screen()
        if args.dump:
            font = board.font()
    if args.screen or args.color:
        print("\n".join(color_lines(words) if args.color else screen_lines(words)))
    if args.dump:
        with open(args.dump, "w") as f:
            for addr, w in [(SCREEN, words)] + font:
                f.write("\n".join(dump_lines(addr, w)) + "\n")
    board.close()
    if not board.returned:
        sys.exit(f"board.py: the program did not return within {args.timeout:g} s")


if __name__ == "__main__":
    main()
