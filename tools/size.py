#!/usr/bin/env python3
"""size.py - reports the size of a NICE program built by nicebuild.py.

Usage: size.py [--emu NICEEMU --input FILE [--run FILE] [--stacks-of OTHER.bin]]
               [--budget WORDS [--check]] program.bin

Reads program.bin, and the program.sym and program.wasm that
nicebuild.py --symbols --keep leaves beside it. The figures are from
the lowest address of the .bin file, 0x8000 for a program built with
--monitor. Prints, in words:

- the code: the translated program, the runtime, and libc, most of
  which comes before ___wasm_memory, and some after the memory;
- the linear memory: its pages, or the bytes of them that the program
  reserves (nicebuild.py --memory-bytes, which the listing program.nasm
  states), the wasm stack, the data segments, and the zero-initialized
  data after them;
- the end of the image, which the .bin file gives, and the end of the
  program with all of its linear memory.

With --emu and --input, it runs the program in niceemu with that console
input (or the file of --run, such as the program joined after a stand-in
for a monitor), and reads the peak use of the wasm stack and of the NICE stack
from the memory after the run: the wasm stack is cleared at the start,
and niceemu's memory starts at 0, so the lowest word that is not 0 is the
deepest the stack has gone. Words the program wrote as 0 are not seen,
so the figures are a lower bound, close in practice. They are the peaks
of that input, not a bound: the NICE stack grows with the depth of the
search.

With --stacks-of, the run is of another program, OTHER.bin (or the
file of --run), and its peaks are counted for program.bin: for a
program that does not run in niceemu, such as one that needs the
network, beside one with the same engine that does.

With --budget, it compares the program, its memory, and the NICE stack
with a budget in words. With --check too, it exits with 1 if they do
not fit, or if the peak of the wasm stack is all of the program's wasm
stack. It needs the run, as without one no NICE stack is counted.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

PAGE_WORDS = 16384
NICE_SP = 0xFFFFFF00   # The NICE stack pointer at start, from start_wasm.nasm
NICE_STACK_SCAN = 8192  # Words below NICE_SP to look at


def leb_u(b: bytes, i: int) -> tuple[int, int]:
    v = shift = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << shift
        shift += 7
        if not c & 0x80:
            return v, i


def leb_s(b: bytes, i: int) -> tuple[int, int]:
    v = shift = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << shift
        shift += 7
        if not c & 0x80:
            if c & 0x40:
                v -= 1 << shift
            return v, i


def read_wasm(path: str) -> dict[str, int]:
    """The initial memory in pages, the initial stack pointer (the first
    global, as clang emits it), and the extent of the data segments."""
    b = open(path, "rb").read()
    i = 8
    info = {"pages": 0, "stack": 0, "data_start": 0, "data_end": 0}
    starts = []
    while i < len(b):
        sid = b[i]
        size, i = leb_u(b, i + 1)
        end = i + size
        if sid == 5:  # Memory
            _, j = leb_u(b, i)
            _, j = leb_u(b, j)  # Flags
            info["pages"], j = leb_u(b, j)
        elif sid == 6 and not info["stack"]:  # Globals: the first is __stack_pointer
            _, j = leb_u(b, i)
            j += 2  # Type and mutability
            if b[j] == 0x41:  # i32.const
                info["stack"], j = leb_s(b, j + 1)
        elif sid == 11:  # Data
            count, j = leb_u(b, i)
            for _ in range(count):
                kind, j = leb_u(b, j)
                if kind == 2:
                    _, j = leb_u(b, j)
                if kind != 1:  # Active: an offset expression
                    assert b[j] == 0x41, "data offset is not i32.const"
                    offset, j = leb_s(b, j + 1)
                    assert b[j] == 0x0B
                    j += 1
                else:
                    offset = -1
                n, j = leb_u(b, j)
                j += n
                if offset >= 0:
                    starts.append(offset)
                    info["data_end"] = max(info["data_end"], offset + n)
        i = end
    info["data_start"] = min(starts) if starts else 0
    return info


def read_bin(path: str) -> tuple[int, int]:
    """The image's lowest address, and one past its highest."""
    low, top = 1 << 32, 0
    with open(path) as f:
        for line in f:
            if line.strip():
                addr = int(line.split()[0], 16)
                low, top = min(low, addr), max(top, addr + 1)
    return low, top


def read_reserved(path: str) -> int | None:
    """The bytes of the linear memory that the program reserves, if
    nicebuild.py --memory-bytes reserved only a part of its pages."""
    try:
        with open(path) as f:
            for line in f:
                m = re.match(r"^; Linear memory: the first (\d+) bytes of", line)
                if m:
                    return int(m.group(1))
    except FileNotFoundError:
        pass
    return None


def read_sym(path: str, name: str) -> int:
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2 and parts[1] == name:
                return int(parts[0], 16)
    sys.exit(f"size.py: {name} not in {path}")


def dump(emu: str, inp: str, binfile: str, ranges: list[tuple[int, int]]) -> dict[int, int]:
    cmd = [emu, "--step-time", "-i", inp]
    for addr, n in ranges:
        cmd += ["-d", f"0x{addr:x}:{n}"]
    out = subprocess.run(cmd + [binfile], stdin=subprocess.DEVNULL, capture_output=True,
                         text=True, check=False).stdout
    mem: dict[int, int] = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-f]{8}):((?: [0-9a-f]{8})+)$", line)
        if m:
            base = int(m.group(1), 16)
            for k, w in enumerate(m.group(2).split()):
                mem[base + k] = int(w, 16)
    if "Halted (HALT)" not in out:
        sys.exit("size.py: the program did not halt normally:\n" + out[-2000:])
    return mem


def layout(binfile: str) -> dict:
    """What read_wasm gives of a program, and, in words from its lowest
    address, low: the start of its memory (membase), the words of the
    memory it reserves, and the end of its image."""
    base = binfile[:-4] if binfile.endswith(".bin") else binfile
    low, image_end = read_bin(binfile)
    w = read_wasm(base + ".wasm")
    reserved = read_reserved(base + ".nasm")
    w.update(low=low, image_end=image_end - low, reserved=reserved,
             membase=read_sym(base + ".sym", "___wasm_memory") - low,
             mem_words=w["pages"] * PAGE_WORDS if reserved is None else (reserved + 3) // 4)
    return w


def main() -> None:
    ap = argparse.ArgumentParser(description="Reports the size of a NICE program.")
    ap.add_argument("bin")
    ap.add_argument("--emu", help="niceemu, to measure the peak stacks")
    ap.add_argument("--input", help="the console input of the measuring run")
    ap.add_argument("--run", help="the file to run in niceemu, if not the program itself")
    ap.add_argument("--stacks-of", metavar="OTHER.bin",
                    help="the program of the measuring run, if not program.bin")
    ap.add_argument("--budget", type=int, help="words for program, memory, and NICE stack")
    ap.add_argument("--check", action="store_true", help="exit with 1 if over the budget")
    args = ap.parse_args()

    if args.check and not (args.emu and args.input):
        sys.exit("size.py: --check needs a run, --emu and --input, for the NICE stack")

    w = layout(args.bin)
    low, image_end, membase = w["low"], w["image_end"], w["membase"]
    reserved, mem_words = w["reserved"], w["mem_words"]
    data_words = (w["data_end"] - w["data_start"] + 3) // 4
    stack_words = w["stack"] // 4

    after = max(0, image_end - (membase + mem_words))  # Runtime placed after the memory
    print(f"base address:     0x{low:08x}")
    print(f"code and runtime:     {membase + after:6d} words  "
          f"({membase} before ___wasm_memory, {after} after the memory)")
    if reserved is None:
        print(f"linear memory:        {mem_words:6d} words  ({w['pages']} pages)")
    else:
        print(f"linear memory:        {mem_words:6d} words  (the first {reserved} bytes "
              f"of {w['pages']} pages)")
    print(f"  wasm stack:         {stack_words:6d} words  (bytes 0 to {w['stack']})")
    print(f"  data segments:      {data_words:6d} words  (bytes {w['data_start']} to {w['data_end']})")
    print(f"  free after data:    {mem_words - (w['data_end'] + 3) // 4:6d} words  "
          f"(zero-initialized data and heap)")
    print(f"image end:            {image_end:6d} words  (the .bin file)")
    total = membase + mem_words + after
    print(f"program end:          {total:6d} words  (with all of its linear memory)")

    nice_peak = 0
    fits = True
    if args.emu and args.input:
        # The program of the run, and its wasm stack, where the peak is read.
        ran = layout(args.stacks_of) if args.stacks_of else w
        ran_low, ran_words = ran["low"] + ran["membase"], ran["stack"] // 4
        mem = dump(args.emu, args.input, args.run or args.stacks_of or args.bin,
                   [(ran_low, ran_words), (NICE_SP - NICE_STACK_SCAN, NICE_STACK_SCAN)])
        unused = next((k for k in range(ran_words) if mem.get(ran_low + k, 0)), ran_words)
        wasm_peak = ran_words - unused
        nlow = next((a for a in range(NICE_SP - NICE_STACK_SCAN, NICE_SP) if mem.get(a, 0)), NICE_SP)
        nice_peak = NICE_SP - nlow
        if args.stacks_of:
            print(f"the peaks are those of {args.stacks_of}:")
        print(f"peak wasm stack:      {wasm_peak:6d} words  (of {stack_words})")
        print(f"peak NICE stack:      {nice_peak:6d} words")
        if wasm_peak >= stack_words:
            print(f"wasm stack:           {stack_words:6d} words, the peak is {wasm_peak}: does NOT fit")
            fits = False

    if args.budget:
        need = total + nice_peak
        if need > args.budget:
            fits = False
        verdict = "fits" if need <= args.budget else "does NOT fit"
        print(f"budget:               {args.budget:6d} words, needs {need}: {verdict} "
              f"({args.budget - need:+d})")
    if args.check and not fits:
        sys.exit(1)


if __name__ == "__main__":
    main()
