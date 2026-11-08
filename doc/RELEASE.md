# Releasing nice_chess

The steps of a release, in order. Each is cheaper than the ones after
it, so a problem is found early. Do not go on while a step fails.
`tools/release.py` scripts the mechanical parts, which each step names.

## Version names

A release is named `Vmajor.minor`: `V1.0`, `V1.1`, and so on, with a new
major version when a change asks for it. The names are nice_chess's own,
not those of nice_cpu or NiceMonMEGA65. Each release's notes name the
nice_cpu tag it is built with, and the NiceMonMEGA65 release it was
checked on and needs.

The front ends show the version: the banner of the text front end, its
UCI `id name`, and the title of the window. A build of a tagged commit
shows the tag, such as `V1.0`. Other builds show what `git describe`
says, such as `V1.0-3-g55970b7`, with `-dirty` for a changed tree. The
release workflow sets `VERSION` to the tag.

## What a release has

- An annotated tag, whose message is the tag's section of
  [RELEASE_NOTES.md](../RELEASE_NOTES.md).
- A GitHub release with that section as its text, and two files:
  - `nice_chess-V1.0.tar.gz`, with `nice_chess-V1.0/`:
    - `mega65/CHESS.BIN` and `mega65/CHESST.BIN`, the windowed and
      the text front ends for the MEGA65, under NiceMonMEGA65's
      monitor, with 8.3 names for its `load` from the SD card;
    - `niceemu/chess_text.bin` and `niceemu/chess_screen.bin`, the
      front ends for niceemu;
    - `linux-x86_64/nice_chess`, the text front end on the host,
      without asserts, as a UCI engine;
    - `README.md`, `RELEASE_NOTES.md`, and `LICENSE`.
  - `SHA256SUMS`, the SHA-256 sum of the archive.

`make dist` makes both in `build/dist/`.

## What you need

- What `make test` needs ([CONTRIBUTING.md](../CONTRIBUTING.md)).
- `gh`, logged in to an account that may push to the repository.
- A MEGA65 under NiceMonMEGA65's monitor, at the release whose notes
  will be named, on its Ethernet port (`ETH=IP`) or its debug UART
  (`PORT=DEVICE`), and a checkout of NiceMonMEGA65 for its tools
  (`NICEMON=DIR`, `../NiceMonMEGA65` by default).

## 1. Prepare

Run `python3 tools/release.py preflight`. It checks items 1 and 3, and
lists the open issues for item 2.

1. The branch is `main`, the working tree is clean, and `HEAD` is
   `origin/main`.
2. No open [issue](https://github.com/MJoergen/nice_chess/issues) blocks
   the release, or the release notes say why it is acceptable.
3. The tools are on the PATH, `make deps` has fetched the pins, and the
   NiceMonMEGA65 checkout is there.

A raise of a pin, such as `NICE_CPU_TAG`, is a commit of its own before
the release, not part of it.

## 2. Documentation

1. Each document says what is true now: [README.md](../README.md),
   [CONTRIBUTING.md](../CONTRIBUTING.md), [src/README.md](../src/README.md),
   [CLAUDE.md](../CLAUDE.md), and `make help`, which agree with the
   Makefile. What happened goes in [HISTORY.md](HISTORY.md) or
   [LOG.md](../LOG.md).
2. The figures quoted (the time of a move on the board, the depth, the
   size) are what the builds of this commit give.
3. The versions in README.md, such as the niceemu of `NICE_CPU_TAG`,
   are those of the Makefile.
4. [RELEASE_NOTES.md](../RELEASE_NOTES.md) has a section for the new
   tag: what a player or a user of the builds can see changed since the
   last release, what it needs first, and the nice_cpu tag.
   `release.py notes` drafts it from the pins, the stamp of step 4, and
   the commits; the text is yours. Before the tag, its heading gets the
   date of the release: `## V1.0 (2026-11-06)`. Documents may be committed after
   the stamp of step 4: `tag` accepts commits since the stamp that change
   only `*.md` files and `doc/`.

## 3. Quick checks

What CI runs ([test.yml](../.github/workflows/test.yml)), so a green run
on the commit is the same evidence, but run them locally too:

    make test perft-deep screen-game match-random size

## 4. The board

    make release-check ETH=192.168.1.65     # or PORT=/dev/ttyUSB3

This runs `release.py check`: on a clean tree, `make board-test`, the
tests of both front ends on the board, and then `make board-match`, two
games of the board against the host build at 5000 nodes a move (about
15 minutes). Each must pass: in the match, no move may be illegal. The
logs are in `build/release/`. Then it writes `build/release/stamp.json`
with the commit, the link, the NiceMonMEGA65 checkout's `git describe`,
and the match's score. `tag` needs it, and `make clean` deletes it.

The board runs the commit's builds, whose version is `git describe`'s,
not the tag. They differ from the release's only in that string.

## 5. CI

Push, and wait for every job of `test.yml` to pass on that commit:

    python3 tools/release.py ci --wait

## 6. Tag

    python3 tools/release.py tag            # or tag V2.0 for a new major version

It refuses unless `HEAD` is clean and is `origin/main`, the name is
`Vmajor.minor` and comes after the last tag, the stamp of step 4 covers
`HEAD`, RELEASE_NOTES.md has a section for the tag with a dated heading,
and CI passed. It takes the next minor version, or `V1.0` for the first,
unless given a name. It shows the section, asks before it makes the
annotated tag with the section as its message, and asks again before it
pushes. By hand:

    git tag -a --cleanup=whitespace V1.0 -m "<the section>"
    git push origin V1.0

## 7. The release workflow

The push of the tag starts
[release.yml](../.github/workflows/release.yml). With `VERSION` set to
the tag, it runs `make test` and `make size`, then `make dist`, and
creates the GitHub release with the section as its text, and the two
files. Wait for it, and look at the release:

    gh run watch $(gh run list --workflow release.yml --limit 1 --json databaseId -q '.[0].databaseId')
    gh release view V1.0

If it failed on a passing cause, such as the network, rerun it with
`gh run rerun`. Otherwise run `make dist VERSION=V1.0` on a checkout of
the tag, and create the release by hand, as release.yml does:

    python3 tools/release.py section V1.0 > section.md
    tail -n +2 section.md > notes.md
    gh release create V1.0 --verify-tag --title "$(head -n 1 section.md)" \
       --notes-file notes.md build/dist/nice_chess-V1.0.tar.gz build/dist/SHA256SUMS

A tag is never moved, and a release is not deleted. A fix is the next
release, or the release notes say what is wrong.
