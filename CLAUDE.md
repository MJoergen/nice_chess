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
  text front end as a UCI engine of the host, `screen_uci.py` the
  windowed front end, played through its keys (`lichess_engine.sh` runs
  it for lichess-bot, with the config template `lichess-bot.yml`),
  `screen_game.py` plays a game through the keys, `size.py` checks the size, `screenshot.py` draws a
  screen dump as a PNG, in the font it selects, `mkpieces.py` draws
  the pieces as glyphs into `src/ui/pieces.h` (`make pieces`).
  `release.py` scripts the release (`test_release.py` tests it, in
  `make test`).
  `analyze.py` has Stockfish score the games of a PGN, and reports
  where the engine loses (`make analyze PGNFILE=FILE`).
  `pgn.py` writes the games of `match.py` and `screen_game.py` as PGN
  (`--pgn`; the Makefile's matches write to `games/`, not in git).
  `rate.sh` (shell) is a rating by cutechess-cli and Ordo (`make rate`).
  `prof.c` (C, on the host) counts the steps of each function in
  niceemu's trace (`make profile`). `mkbook.c` (C, on the host) makes
  the table of the opening book from `src/engine/book.txt`.
- `doc/HISTORY.md`: how the port was done, by phases, and the
  decisions, after the fact. It says only what happened; plans and
  open items are GitHub issues, not files or TODO comments.
- `doc/RELEASE.md`: how to make a release; `RELEASE_NOTES.md`: what
  changed in each release, newest first. Add a section for every new tag.
- `BOT.md`: how to run the board as a lichess bot, with lichess-bot.
- `LOG.md`: investigations (measurements, options weighed), newest
  first, dated.
- `CONTRIBUTING.md`: building and the tests, for human contributors.
  Keep it, README.md, and `make help` in sync with the Makefile.
- `deps/` (fetched by `make deps`) and `build/` are not in git.
  `deps/nice_cpu-TAG` is nice_cpu at the pinned tag: the translator
  (`translator/nicebuild.py`), the runtime and libc, and the docs of the
  CPU and toolchain (`doc/ISA.md`, `doc/TOOLCHAIN.md`).

## Commands

    make                # help: the targets
    make deps           # nice_cpu, niceemu, and the test suites into deps/
    make all            # build everything
    make test           # host and niceemu tests; run after every change
    make size           # the MEGA65 builds against the 92K-word budget
    make perft-deep screen-game screen-uci match-random   # slower; in CI too
    make dist           # the archive of a release in build/dist/ (VERSION=V1.0)
    make release-check ETH=IP   # board-test and board-match, the stamp for release.py tag

The board targets (`board-*`) need `ETH=IP` or `PORT=DEVICE` and a
NiceMonMEGA65 checkout (`NICEMON=DIR`). They are not in CI; run them
only when the user asks, or when a change is for the board.

## Conventions

- The engine must keep working on NICE and on the host. The host build
  has asserts and `-DCHECK_STATE`, which checks the incremental state
  (Zobrist key, evaluation sums) and the captures-only move list
  against the full one at every perft node, so it catches
  engine bugs that the NICE builds, without asserts, do not.
- NICE builds are `-O 2 -D NDEBUG --no-bounds-checks`: an access out of
  bounds is not caught there; the host build's asserts are the check.
  Size counts:
  `make size` must pass.
- Speed is measured in niceemu steps, which are deterministic. Report
  figures as steps, and as seconds on the board at about 54 million
  steps a second.
- Change a `*.expected` file only when the output changed on purpose,
  and say how in the commit message.
- Pins (`NICE_CPU_TAG`, the SHA-256 sums, `MCHESS2_COMMIT`) are raised
  by hand, deliberately, not as a side effect.
- The docs are written in short, plain, factual sentences, and say what
  is true now. History goes in doc/HISTORY.md or LOG.md.
- Releases are `Vmajor.minor`, nice_chess's own (doc/RELEASE.md). The
  front ends show `VERSION`, from `git describe`; the tests see it as
  `VERSION`, so a release changes no `*.expected` file.
- Commit messages: a short subject (`Phase N: ...`, `doc: ...`,
  `CI: ...`), then the why.
