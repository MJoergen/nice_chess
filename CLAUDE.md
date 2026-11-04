# CLAUDE.md

nice_chess is a chess engine in C for the NICE CPU (nice_cpu), ported
from mchess2. It runs in niceemu, on the MEGA65 under NiceMonMEGA65's
monitor, and on the host. See README.md.

## Layout

- `src/`: the code; `src/README.md` says what each file does.
  `src/engine/` is the core: no I/O, no clock, no malloc, so the same
  files build for NICE and for the host. `src/platform/` (clock, key
  poll) and `src/ui/` (`text.c`, `screen.c`) are around it.
- `tests/`: the inputs of the tests, `*.grep` (the lines of the output
  that are compared) and `*.expected`. `tools/runtest.sh` runs one test.
- `tools/`: Python 3. `match.py` plays UCI engines against each other,
  `board.py` runs a program on the board, `board_uci.py` is the board's
  engine as a UCI engine of the host, `screen_game.py` plays a game
  through the keys, `size.py` checks the size, `screenshot.py` draws a
  screen dump as a PNG. `rate.sh` (shell) is a rating by cutechess-cli
  and Ordo (`make rate`).
- `doc/HISTORY.md`: how the port was done, by phases, and the
  decisions, after the fact. It says only what happened; plans and
  open items are GitHub issues, not files or TODO comments.
- `LOG.md`: investigations (measurements, options weighed), newest
  first, dated.
- `CONTRIBUTING.md`: building and the tests, for human contributors.
  Keep it, README.md, and `make help` in sync with the Makefile.
- `deps/` (fetched by `make deps`) and `build/` are not in git.
  `deps/nice_cpu` is nice_cpu at the pinned tag: the translator
  (`translator/nicebuild.py`), the runtime and libc, and the docs of the
  CPU and toolchain (`doc/ISA.md`, `doc/TOOLCHAIN.md`).

## Commands

    make                # help: the targets
    make deps           # nice_cpu, niceemu, and the test suites into deps/
    make all            # build everything
    make test           # host and niceemu tests; run after every change
    make size           # the MEGA65 builds against the 92K-word budget
    make perft-deep screen-game match-random   # slower; in CI too

The board targets (`board-*`) need `ETH=IP` or `PORT=DEVICE` and a
NiceMonMEGA65 checkout (`NICEMON=DIR`). They are not in CI; run them
only when the user asks, or when a change is for the board.

## Conventions

- The engine must keep working on NICE and on the host. The host build
  has asserts and `-DCHECK_STATE`, which checks the incremental state
  (Zobrist key, evaluation sums) at every perft node, so it catches
  engine bugs that the NICE builds, without asserts, do not.
- NICE builds are `-O s -D NDEBUG`. Size counts: `make size` must pass.
- Speed is measured in niceemu steps, which are deterministic. Report
  figures as steps, and as seconds on the board at about 54 million
  steps a second.
- Change a `*.expected` file only when the output changed on purpose,
  and say how in the commit message.
- Pins (`NICE_CPU_TAG`, the SHA-256 sums, `MCHESS2_COMMIT`) are raised
  by hand, deliberately, not as a side effect.
- The docs are written in short, plain, factual sentences, and say what
  is true now. History goes in doc/HISTORY.md or LOG.md.
- Commit messages: a short subject (`Phase N: ...`, `doc: ...`,
  `CI: ...`), then the why.
