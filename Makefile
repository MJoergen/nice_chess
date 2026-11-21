# nice_chess: a chess engine for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag, and
# the perft suite from mchess2 at a pinned commit (doc/HISTORY.md,
# Decisions). make deps fetches them into deps/. NICE_CPU=DIR and
# NICEEMU=PATH use local copies of nice_cpu and niceemu instead.
# make help lists the targets.

NICE_CPU_TAG    := V2.2
NICEEMU_SHA256  := c37108d1a5b7380ed1a8d2ca5400c452f8851cd85cc342fc22c7f1878071b9d6
MCHESS2_COMMIT  := 65e88554b20e06079301ef2314f6559fab06d062
PERFTSUITE_SHA256 := 363e5205dfff3205fc50b376fb1770f9156464456282372c8c0a0e5b45a0bf07
SEARCHSUITE_SHA256 := 291bd3110550fbb26f8c887e4ed23249200d044b52d912b573b1875351b83b0a
# The openings of make rate: 8moves_v3.pgn of fishtest's books.
BOOKS_COMMIT    := 2ec69181c0b1367c5c58bcf5304a79c7b198b655
BOOK_ZIP_SHA256 := 7e1e9dd118b4bb97d8a8b5b8a790c86e21f8509d59a27d2883767d94477be02e

NICE_CPU ?= deps/nice_cpu-$(NICE_CPU_TAG)
NICEEMU  ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
NICEMON  ?= ../NiceMonMEGA65
PYTHON   ?= python3
CC       ?= cc
# The version the front ends show: the release tag on a tagged commit,
# such as V1.0, and git describe's name of the commit between tags.
# The release workflow sets it to the tag. tools/runtest.sh replaces it
# with VERSION in the output, so the tests do not depend on it.
VERSION  ?= $(shell git describe --tags --match 'V[0-9]*' --always --dirty 2>/dev/null || echo unknown)
export NICEEMU NICEMON VERSION

# How to reach the board: ETH=IP or PORT=DEVICE, as NiceMonMEGA65's tools
# take them. Only the board targets need one.
BOARDLINK = $(if $(ETH),--eth $(ETH),$(if $(PORT),--port $(PORT),$(error ETH=IP or PORT=DEVICE names the board)))
LEVEL ?= 2

NICEBUILD  = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz
PERFTSUITE  = deps/mchess2/perftsuite.epd
SEARCHSUITE = deps/mchess2/searchsuite.epd
# The nodes a move of tests/matesuite.c, around those of the levels.
MATESUITE_NODES = 1000 3000 10000 18000 20000 30000 60000 93000
MCHESS2_RAW = https://raw.githubusercontent.com/MJoergen/mchess2/$(MCHESS2_COMMIT)

# mchess2's sources, to build it as an opponent (make match-mchess2)
MCHESS2_SRC = main.cc CBoard.cc CMove.cc ai.cc CMoveList.cc CPerft.cc \
	CHashEntry.cc CHashTable.cc CPerftSuite.cc CSearchSuite.cc \
	CBoard.h CHashEntry.h CHashTable.h CMove.h CMoveList.h CPerft.h \
	CPerftSuite.h CSearchSuite.h CSquare.h CTime.h ai.h crc32.h trace.h

# The MEGA65 gives a program under the monitor the words 0x8000 to
# 0x1E7FF, 90K, for its image, its linear memory, and the NICE stack,
# which the monitor's routines share and use at most 32 words of, and
# BASIC's SYS 13 more; BASIC's variables are above, from 0x1E800
# (NiceMonMEGA65 V2.4, doc/SYSTEM.md).
BUDGET = 92115
# The Nexys4DDR gives 0x8000 to 0xE7FF, 26K words, less the same 45.
NEXYS_BUDGET = 26579

# The engine, and the table of its opening book, which tools/mkbook.c
# makes from src/engine/book.txt with the engine's own keys.
BOOKDATA = build/gen/book_data.c
CORE = $(wildcard src/engine/*.c)
ENGINE = $(CORE) $(BOOKDATA)
# The headers, and the Makefile, whose flags every build takes, so that
# a change of the flags rebuilds.
HEADERS = $(wildcard src/engine/*.h) src/platform/platform.h Makefile
# The version, for the front ends (VERSION).
VERSIONC = build/gen/version.c

# NICE: optimized for speed, without asserts, and without the bounds
# checks of wasm's memory accesses, which the host builds' asserts and
# CHECK_STATE stand in for (LOG.md, 2026-11-04). The engine keeps its
# move lists in its own data, so the wasm stack is small. The memory is
# three pages, 192 KB, for the transposition table's 128 KB (TT_BITS).
# --trust-alignment leaves out the alignment test of each access of 4
# bytes, as clang's alignment hints are exact. --byte-windows reaches
# the memory through the byte windows of nice_cpu's SYSTEM.md §9, which
# niceemu has from V2.0 on, and NiceMonMEGA65 from V2.0 on (LOG.md,
# 2026-11-11).
NICEFLAGS = -O 2 -D NDEBUG --no-bounds-checks --trust-alignment --byte-windows \
            --stack 8192 --memory 196608 --symbols --keep
# The MEGA65: the same, built for the monitor, which loads it at 0x8000.
MONFLAGS = $(NICEFLAGS) --monitor
# The minimal build (src/ui/mini.c), for the Nexys4DDR's 26K words
# (#28): optimized for size, a transposition table of 256 entries
# (TT_BITS), and one page of memory, of which it reserves only up to
# the end of its static data (--memory-bytes auto). The wasm stack stays
# 8 KB, as the opening book keeps about 3 KB of moves on it.
MINIFLAGS = -O z -D NDEBUG -D TT_BITS=8 --no-bounds-checks --trust-alignment --byte-windows \
            --stack 8192 --memory 65536 --memory-bytes auto --symbols --keep
MINI = src/ui/mini.c src/ui/miniuci.c
# Remote play's player whose engine plays (#27).
PEER = src/ui/peer.c src/ui/remote.c
# The peer for the Nexys4DDR: as the minimal build, with a wasm stack
# of 4 KB, which leaves its 1024 words to the NICE stack. A search
# keeps 25 words on the NICE stack for each ply, and 16 bytes on the
# wasm stack, to 63 plies; the book's 3 KB is the most on the wasm
# stack (LOG.md, 2026-11-21).
NEXYSPEERFLAGS = $(subst --stack 8192,--stack 4096,$(MINIFLAGS))
# The network on the boards: the monitor's UDP entries, through the stubs
# of src/platform/net.nasm, for the builds of remote play under the monitor.
PEERNET = -D NICE_NET --asm src/platform/net.nasm

# The host: with asserts, and a check of the Zobrist key, the
# evaluation's sums, and the captures-only move list at every node
# of perft.
HOSTFLAGS = -O2 -W -Wall -Wextra -DCHECK_STATE
# The host, for matches and the release: without asserts and the check,
# which play the same moves, only faster, and linked statically, so that
# the release's binary runs on any x86_64 Linux. It needs glibc's
# libc.a: libc6-dev on Debian and Ubuntu, glibc-static on Fedora.
FASTFLAGS = -O2 -W -Wall -Wextra -DNDEBUG -static

# A rating, by tools/rate.sh: cutechess-cli and Ordo, and the opponents.
# The board's search at 5 seconds a move, the time of go and of level 3,
# is about 172000 nodes (LOG.md); the other levels get their share of it.
CUTECHESS ?= cutechess-cli
ORDO      ?= ordo
STOCKFISH ?= stockfish
NODES     ?= 172000
LEVELS    ?= 1 2 3 4 5
STRENGTHS ?=
OPPONENTS ?=
ELOS      ?= 1600 1800 2000 2200
GAMES     ?= 100
CONCURRENCY ?= 4
BOOK      ?= deps/books/8moves_v3.pgn

# The games of every match go to PGNDIR, in a file of their own for
# each run, named by the target and the time it started:
# games/rate-20261111-143000.pgn. PGNDIR is not in git, and make clean
# keeps it.
PGNDIR    ?= games
STAMP     := $(shell date +%Y%m%d-%H%M%S)
PGN        = $(PGNDIR)/$@-$(STAMP).pgn
export CUTECHESS ORDO STOCKFISH NODES LEVELS STRENGTHS ELOS OPPONENTS GAMES CONCURRENCY BOOK

.PHONY: FORCE help all test pieces host-test nice-test tools-test perft-deep remote-relay remote-gone screen-game screen-uci match-random match-mini match-mchess2 rate analyze openings size profile deps clean distclean
.PHONY: board-text board-screen board-test board-game board-match board-stockfish board-remote dist release-check
help:
	@echo "nice_chess: a chess engine for the NICE system"
	@echo ""
	@echo "  make help          show this text (the default)"
	@echo "  make deps          fetch nice_cpu $(NICE_CPU_TAG), niceemu $(NICE_CPU_TAG), and mchess2's test suites into deps/"
	@echo "  make all           build both front ends, for niceemu, for the MEGA65, and for the host,"
	@echo "                     the minimal build, for niceemu, for the Nexys4DDR, and for the host,"
	@echo "                     and the player of remote play whose engine plays, for the host, the Nexys4DDR, and the MEGA65"
	@echo "  make test          the tests, on the host and in niceemu, and of tools/release.py"
	@echo "  make perft-deep    the perft suite to depth 5 on the host (a minute)"
	@echo "  make remote-relay  remote play on the host, through a relay that drops and doubles datagrams (30 seconds)"
	@echo "  make remote-gone   remote play on the host, with the other board switched off in a game (30 seconds)"
	@echo "  make screen-game   a full game in the windowed front end, in niceemu (a minute)"
	@echo "  make screen-uci    the windowed front end as a UCI engine, by its keys, in niceemu (2 games, 2 minutes)"
	@echo "  make match-random  the host build against random moves (20 games)"
	@echo "  make match-mini    the minimal build in niceemu against random moves (2 games, a minute)"
	@echo "  make match-mchess2 the host build against mchess2 (20 games, 15 minutes)"
	@echo "  make openings      fetch the openings of make rate, 8moves_v3.pgn, into deps/books/"
	@echo "  make rate          a rating of each level: cutechess-cli games against Stockfish's UCI_Elo levels, and Ordo"
	@echo "                     (STRENGTHS=\"1 2 3 4 5\" rates the strengths too, LEVELS= no level)"
	@echo "                     (OPPONENTS=\"NAME=ELO=COMMAND ...\" adds engines anchored at ELO)"
	@echo "  make analyze       Stockfish's scores of the games of PGNFILE=FILE, and the patterns in PLAYER's play"
	@echo "                     (PLAYER=nice_chess; MOVETIME=1000 ms a position; about 10 minutes for 100 games)"
	@echo "  make size          the size of the MEGA65 builds and of the Nexys4DDR's, against the boards' budgets"
	@echo "  make profile       the steps of each function in niceemu, on the 24 Bratko-Kopec positions at 3000 nodes"
	@echo "                     (BIN=, PROFILE_INPUT= another program or input)"
	@echo "  make pieces        draw the pieces again into src/ui/pieces.h (needs Pillow)"
	@echo "  make dist          the archive of a release, and SHA256SUMS, in build/dist/ (VERSION=V1.0)"
	@echo "  make clean         remove build/"
	@echo "  make distclean     remove build/ and deps/"
	@echo ""
	@echo "On the MEGA65, under NiceMonMEGA65's monitor, with ETH=IP (the board's"
	@echo "Ethernet port) or PORT=/dev/ttyUSBn (its debug UART), and the tools of a"
	@echo "checkout of NiceMonMEGA65 (NICEMON=DIR, ../NiceMonMEGA65 by default):"
	@echo ""
	@echo "  make board-text    load and run the text front end, then talk to it over the line"
	@echo "  make board-screen  load and run the windowed front end, for the screen and keyboard"
	@echo "  make board-test    the tests of both front ends, on the board"
	@echo "  make board-game    a full game in the windowed front end, on the board (a few minutes)"
	@echo "  make board-match   the text front end on the board against the host build, in UCI"
	@echo "  make board-remote  remote play: the text front end at ETH against the peer at ETH2=IP, over UDP"
	@echo "                     (a Nexys4DDR's by default, PEER2=build/mega65/chess_peer.bin for a MEGA65)"
	@echo "  make board-stockfish  a game on the board's screen against Stockfish (ELO=1800, MOVETIME=1000 ms, BLACK=1: it is black)"
	@echo "  make release-check board-test and board-match, and the stamp that tools/release.py tag needs"
	@echo "                    (doc/RELEASE.md)"
	@echo ""
	@echo "The matches (match-*, screen-uci, rate, board-match, board-game, board-stockfish, board-remote) write their games"
	@echo "to games/TARGET-YYYYMMDD-HHMMSS.pgn (PGNDIR=DIR another directory)."
	@echo ""
	@echo "NICE_CPU=DIR and NICEEMU=PATH use local copies of nice_cpu and niceemu."

# The NICE builds depend on the toolchain: its directory and commit, in
# a file that changes only when they do, so that a new pin or another
# NICE_CPU=DIR builds them again.
TOOLCHAIN = build/toolchain.txt
$(TOOLCHAIN): FORCE | $(NICE_CPU)
	@mkdir -p $(@D)
	@t="$(abspath $(NICE_CPU)) $$(git -C $(NICE_CPU) rev-parse HEAD 2>/dev/null)"; \
	  [ "$$t" = "$$(cat $@ 2>/dev/null)" ] || echo "$$t" > $@

$(VERSIONC): FORCE
	@mkdir -p $(@D)
	@t='const char nice_chess_version[] = "$(VERSION)";'; \
	  [ "$$t" = "$$(cat $@ 2>/dev/null)" ] || echo "$$t" > $@

FORCE:

all: build/nice/chess_text.bin build/nice/chess_screen.bin build/host/chess_text \
     build/mega65/chess_text.bin build/mega65/chess_screen.bin \
     build/nice/chess_mini.bin build/nexys/chess_mini.bin build/host/chess_mini build/host/chess_peer \
     build/nexys/chess_peer.bin build/mega65/chess_peer.bin

build/nice/chess_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/ui/remote.c src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/ui/remote.c src/platform/nice.c

build/nice/chess_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/ui/pieces.h src/ui/remote.c src/ui/remote.h src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/screen.c src/ui/remote.c src/platform/nice.c

build/mega65/chess_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/ui/remote.c src/platform/nice.c src/platform/net.nasm $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) $(PEERNET) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/ui/remote.c src/platform/nice.c

build/mega65/chess_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/ui/pieces.h src/ui/remote.c src/ui/remote.h src/platform/nice.c src/platform/net.nasm $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) $(PEERNET) -o $@ $(ENGINE) $(VERSIONC) src/ui/screen.c src/ui/remote.c src/platform/nice.c

build/nice/chess_mini.bin: $(ENGINE) $(HEADERS) $(MINI) src/ui/miniuci.h src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MINIFLAGS) -o $@ $(ENGINE) $(VERSIONC) $(MINI) src/platform/nice.c

build/nexys/chess_mini.bin: $(ENGINE) $(HEADERS) $(MINI) src/ui/miniuci.h src/platform/nice.c $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MINIFLAGS) --monitor -o $@ $(ENGINE) $(VERSIONC) $(MINI) src/platform/nice.c

# nice_cpu's stand-in for a monitor at 0, which runs a program at 0x8000
# as the monitor does, so that the MEGA65 builds run in niceemu too,
# joined after it (a .bin file is address and word pairs).
build/nice/monstub.bin: $(NICE_CPU)/runtime/test/monitor.nasm $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(PYTHON) $(NICE_CPU)/assembler/niceasm.py -o $@ $<

# Remote play's player whose engine plays (src/ui/peer.c), with the
# network (PEERNET): for the Nexys4DDR, in
# its 26K words, as the minimal build, and for the MEGA65.
build/nexys/chess_peer.bin: $(ENGINE) $(HEADERS) $(PEER) src/ui/remote.h src/platform/nice.c src/platform/net.nasm $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NEXYSPEERFLAGS) --monitor $(PEERNET) -o $@ $(ENGINE) $(VERSIONC) $(PEER) src/platform/nice.c

build/mega65/chess_peer.bin: $(ENGINE) $(HEADERS) $(PEER) src/ui/remote.h src/platform/nice.c src/platform/net.nasm $(VERSIONC) $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) $(PEERNET) -o $@ $(ENGINE) $(VERSIONC) $(PEER) src/platform/nice.c

build/test/mon_%.bin: build/nice/monstub.bin build/mega65/%.bin
	@mkdir -p $(@D)
	cat $^ > $@

build/test/nexys_%.bin: build/nice/monstub.bin build/nexys/%.bin
	@mkdir -p $(@D)
	cat $^ > $@

build/nice/perftsuite.bin: $(ENGINE) $(HEADERS) tests/perftsuite.c $(TOOLCHAIN)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -D PERFT_MAX_DEPTH=3 -o $@ $(ENGINE) tests/perftsuite.c

# The glyphs of the pieces. pieces.h is in git, so that a build needs
# no Pillow; make pieces draws it again after a change to mkpieces.py.
pieces:
	$(PYTHON) tools/mkpieces.py src/ui/pieces.h

build/host/mkbook: $(CORE) $(HEADERS) tools/mkbook.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(CORE) tools/mkbook.c

build/host/prof: tools/prof.c
	@mkdir -p $(@D)
	$(CC) -O2 -W -Wall -Wextra -o $@ tools/prof.c

# The profile: tools/prof.c over niceemu's trace. By default the text
# front end for niceemu on the 24 Bratko-Kopec positions of the search
# suite, at go nodes 3000 each.
BIN           ?= build/nice/chess_text.bin
PROFILE_INPUT ?= build/test/bk.input
build/test/bk.input: $(SEARCHSUITE)
	@mkdir -p $(@D)
	(echo uci; grep 'id "BK\.' $(SEARCHSUITE) \
	  | awk '{ print "position fen " $$1 " " $$2 " " $$3 " " $$4 " 0 1"; print "go nodes 3000" }') > $@

profile: build/host/prof $(BIN) $(PROFILE_INPUT) | $(NICEEMU)
	$(NICEEMU) -t -i $(PROFILE_INPUT) $(BIN) | build/host/prof $(basename $(BIN)).sym

$(BOOKDATA): build/host/mkbook src/engine/book.txt
	@mkdir -p $(@D)
	build/host/mkbook < src/engine/book.txt > $@.tmp
	mv $@.tmp $@

build/host/chess_text: $(ENGINE) $(HEADERS) src/ui/text.c src/ui/remote.c src/platform/host.c $(VERSIONC)
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/ui/remote.c src/platform/host.c

build/host/chess_mini: $(ENGINE) $(HEADERS) $(MINI) src/ui/miniuci.h src/platform/host.c $(VERSIONC)
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -DTT_BITS=8 -o $@ $(ENGINE) $(VERSIONC) $(MINI) src/platform/host.c

# The player of remote play whose engine plays (src/ui/peer.c), which
# the remote tests run twice, over the loopback.
build/host/chess_peer: $(ENGINE) $(HEADERS) $(PEER) src/ui/remote.h src/platform/host.c $(VERSIONC)
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -DTT_BITS=8 -o $@ $(ENGINE) $(VERSIONC) $(PEER) src/platform/host.c

build/host/chess_fast: $(ENGINE) $(HEADERS) src/ui/text.c src/ui/remote.c src/platform/host.c $(VERSIONC)
	@mkdir -p $(@D)
	$(CC) $(FASTFLAGS) -o $@ $(ENGINE) $(VERSIONC) src/ui/text.c src/ui/remote.c src/platform/host.c

build/host/perftsuite: $(ENGINE) $(HEADERS) tests/perftsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/perftsuite.c

build/host/searchsuite: $(ENGINE) $(HEADERS) tests/searchsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/searchsuite.c

build/host/matesuite: $(ENGINE) $(HEADERS) tests/matesuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/matesuite.c

test: host-test nice-test tools-test

# The tests of the tools: tools/test_*.py.
tools-test:
	$(PYTHON) -m unittest discover -s tools -p 'test_*.py'

# tests/mini.input with the lines ended as a terminal ends them, at CR
# and at CR LF by turns: the replies must be the same.
build/test/mini_cr.input: tests/mini.input
	@mkdir -p $(@D)
	awk '{ printf "%s%s", $$0, NR % 2 ? "\r" : "\r\n" }' $< > $@

host-test: build/host/chess_text build/host/chess_mini build/host/chess_peer build/host/perftsuite build/host/searchsuite build/host/matesuite $(PERFTSUITE) $(SEARCHSUITE) build/test/mini_cr.input
	@tools/runtest.sh host_text text tests/text.input host build/host/chess_text
	@tools/runtest.sh host_search search tests/search.input host build/host/chess_text
	@tools/runtest.sh host_search_end search_end tests/search_end.input host build/host/chess_text
	@tools/runtest.sh host_book book tests/book.input host build/host/chess_text
	@tools/runtest.sh host_mini mini tests/mini.input host build/host/chess_mini
	@tools/runtest.sh host_mini_cr mini build/test/mini_cr.input host build/host/chess_mini
	@$(PYTHON) tools/remote_test.py host_remote remote build/host/chess_peer
	@$(PYTHON) tools/remote_test.py host_remote_text remote_text build/host/chess_peer --text build/host/chess_text
	@$(PYTHON) tools/remote_talk.py host_remote_talk tests/remote_talk.input build/host/chess_text
	@tools/runtest.sh host_perftsuite4 perftsuite4 $(PERFTSUITE) host build/host/perftsuite 4
	@tools/runtest.sh host_searchsuite searchsuite $(SEARCHSUITE) host build/host/searchsuite 10000
	@tools/runtest.sh host_matesuite matesuite tests/matesuite.input host build/host/matesuite $(MATESUITE_NODES)
	@tools/runtest.sh host_matesuite_bn matesuite_bn tests/matesuite_bn.input host build/host/matesuite $(MATESUITE_NODES)

nice-test: build/nice/chess_text.bin build/nice/chess_screen.bin build/nice/perftsuite.bin $(PERFTSUITE) \
           build/test/mon_chess_text.bin build/test/mon_chess_screen.bin \
           build/nice/chess_mini.bin build/test/nexys_chess_mini.bin | $(NICEEMU)
	@tools/runtest.sh nice_screen screen tests/screen.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_screen_book screen_book tests/screen_book.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_screen_clock screen_clock tests/screen_clock.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_screen_ponder screen_ponder tests/screen_ponder.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_screen_undo screen_undo tests/screen_undo.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_text text tests/text.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_search search tests/search.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_search_end search_end tests/search_end.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_book book tests/book.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_perftsuite3 perftsuite3 $(PERFTSUITE) nice build/nice/perftsuite.bin
	@tools/runtest.sh mega65_screen screen tests/screen.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_screen_book screen_book tests/screen_book.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_screen_clock screen_clock tests/screen_clock.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_screen_ponder screen_ponder tests/screen_ponder.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_screen_undo screen_undo tests/screen_undo.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_text text tests/text.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh mega65_search search tests/search.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh mega65_search_end search_end tests/search_end.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh mega65_book book tests/book.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh nice_mini mini tests/mini.input nice build/nice/chess_mini.bin
	@tools/runtest.sh nexys_mini mini tests/mini.input nice build/test/nexys_chess_mini.bin

# Remote play (#27): the game of host_remote, through a relay that
# drops every fifth datagram and doubles every seventh, which must
# change nothing. Each drop costs the 500 ms of a resend.
remote-relay: build/host/chess_peer
	@$(PYTHON) tools/remote_test.py host_remote_relay remote build/host/chess_peer --relay

# Remote play (#27): the other board is switched off in a game of two
# text front ends. The one left says gone after 30 seconds, its wait
# for the turn is over, and it ends at the end of its input.
remote-gone: build/host/chess_text
	@$(PYTHON) tools/remote_talk.py --wait 45 host_remote_gone tests/remote_gone.input build/host/chess_text

perft-deep: build/host/perftsuite $(PERFTSUITE)
	@tools/runtest.sh host_perftsuite5 perftsuite5 $(PERFTSUITE) host build/host/perftsuite 5

# The keys of a game that tools/screen_game.py played with its defaults
# (white at 2000 nodes, level 1), to checkmate by white at move 39.
screen-game: build/nice/chess_screen.bin | $(NICEEMU)
	@tools/runtest.sh nice_screen_game screen_game tests/screen_game.keys screen build/nice/chess_screen.bin

# tools/screen_uci.py plays the windowed front end by its keys, in
# niceemu at level 1, against the host build at 2000 nodes a move: two
# games from an opening, which it sets up through the keys too. With no
# text front end to play on with, a move the front end does not take,
# or one that is not legal, fails it.
screen-uci: build/nice/chess_screen.bin build/host/chess_text | $(NICEEMU)
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/match.py --openings 1 --go1 go --opt1 Level=1 --go2 "go nodes 2000" --pgn $(PGN) \
	  build/host/chess_text "$(PYTHON) tools/screen_uci.py build/nice/chess_screen.bin" build/host/chess_text

# Against random moves, at 20000 nodes a move: it must win every game.
match-random: build/host/chess_text
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/match.py --go1 "go nodes 20000" --min-score 20 --pgn $(PGN) \
	  build/host/chess_text build/host/chess_text random

# The minimal build in niceemu against random moves, at its default
# go, 20000 nodes: two games, which it must win.
match-mini: build/nice/chess_mini.bin build/host/chess_text | $(NICEEMU)
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/match.py --openings 1 --go1 go --min-score 2 --pgn $(PGN) \
	  build/host/chess_text "$(NICEEMU) --step-time build/nice/chess_mini.bin" random

# Against mchess2, at 40 moves in 20 seconds each.
match-mchess2: build/host/chess_text build/host/mchess2
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/match.py --time 20 --pgn $(PGN) build/host/chess_text build/host/chess_text build/host/mchess2

# A rating of each of the 5 levels of the board's engine (tools/rate.sh).
rate: build/host/chess_fast $(BOOK)
	@mkdir -p $(PGNDIR)
	PGN=$(PGN) tools/rate.sh

# The patterns in an engine's games, by Stockfish at full strength
# (tools/analyze.py): PGNFILE=FILE, as cutechess-cli writes it, and
# PLAYER, the engine's name in its tags. The scores are kept in
# build/analyze/.
PLAYER ?= nice_chess
analyze: build/host/chess_text
	$(if $(PGNFILE),,$(error PGNFILE=FILE names the games))
	$(PYTHON) tools/analyze.py --stockfish "$(STOCKFISH)" --movetime $(MOVETIME) $(PGNFILE) $(PLAYER)

openings: deps/books/8moves_v3.pgn

build/host/mchess2: $(addprefix deps/mchess2/src/,$(MCHESS2_SRC))
	@mkdir -p $(@D)
	$(CXX) -O2 -DNAME=mchess2 -o $@ $(filter %.cc,$^)

deps/mchess2/src/%:
	@mkdir -p $(@D)
	curl -sSfL -o $@ $(MCHESS2_RAW)/$*

size: build/mega65/chess_text.bin build/mega65/chess_screen.bin build/test/mon_chess_text.bin build/test/mon_chess_screen.bin \
      build/nexys/chess_mini.bin build/test/nexys_chess_mini.bin build/nexys/chess_peer.bin | $(NICEEMU)
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size.input --run build/test/mon_chess_text.bin \
	  --budget $(BUDGET) --check build/mega65/chess_text.bin
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/screen.keys --run build/test/mon_chess_screen.bin \
	  --budget $(BUDGET) --check build/mega65/chess_screen.bin
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size_mini.input --run build/test/nexys_chess_mini.bin \
	  --budget $(NEXYS_BUDGET) --check build/nexys/chess_mini.bin
	@# The peer has no network in niceemu, and stops. Its stacks are taken
	@# as the minimal build's, which has the same book and the same search,
	@# from a few words deeper.
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size_mini.input --run build/test/nexys_chess_mini.bin \
	  --stacks-of build/nexys/chess_mini.bin --budget $(NEXYS_BUDGET) --check build/nexys/chess_peer.bin

# The board. load.py loads a program under the monitor and types
# SYS 0x8000, which runs it; what it prints then comes on the line, such
# as nc IP 4510 or picocom -b 2000000 DEVICE (NiceMonMEGA65's
# tools/README.md).
board-text: build/mega65/chess_text.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

board-screen: build/mega65/chess_screen.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

# Remote play (#27) between two boards on one network: the text front
# end at ETH, white, by remote auto at 2 seconds a move, against the
# peer at ETH2, black, at its 20000 nodes. The moves and the result
# must be the same in both logs, and legal.
PEER2 ?= build/nexys/chess_peer.bin
board-remote: build/mega65/chess_text.bin $(PEER2) build/host/chess_text
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/board_remote.py $(if $(NICEMON),--nicemon $(NICEMON)) \
	  --eth $(or $(ETH),$(error ETH=IP names the board of the text front end)) \
	  --eth2 $(or $(ETH2),$(error ETH2=IP names the board of the peer)) \
	  --peer $(PEER2) --pgn $(PGN)

board-test: build/mega65/chess_text.bin build/mega65/chess_screen.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_text text tests/text.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_search search tests/search.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_book book tests/book.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_screen screen tests/screen.keys board-screen build/mega65/chess_screen.bin

# A game at LEVEL (2 by default) by the keys, white's moves from the host
# build; the keys, and the screen at the end, go to build/mega65/.
# A game in the windowed front end on the board, against Stockfish at
# UCI_Elo $(ELO) and $(MOVETIME) ms a move, white unless BLACK=1; the board's
# screen shows it as it is played. The engine has its level 3, 5 seconds.
# The host prints each move with Stockfish's score, at full strength.
ELO ?= 1800
MOVETIME ?= 1000
board-stockfish: build/mega65/chess_screen.bin build/host/chess_text
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/screen_game.py $(BOARDLINK) --level 3 --opponent "$(STOCKFISH)" --pgn $(PGN) \
	  --opt UCI_LimitStrength=true --opt UCI_Elo=$(ELO) --movetime $(MOVETIME) $(if $(BLACK),--black) \
	  --eval "$(STOCKFISH)" \
	  build/mega65/chess_screen.bin build/host/chess_text build/mega65/stockfish.keys

board-game: build/mega65/chess_screen.bin build/host/chess_text
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/screen_game.py $(BOARDLINK) --level $(LEVEL) --dump build/mega65/game.dump --pgn $(PGN) \
	  build/mega65/chess_screen.bin build/host/chess_text build/mega65/game.keys

# Two games, one with each color, the board at its default time of a
# move, 5 seconds, and the host build at 5000 nodes a move.
board-match: build/mega65/chess_text.bin build/host/chess_text
	@mkdir -p $(PGNDIR)
	$(PYTHON) tools/match.py $(BOARDLINK) --openings 1 --go1 "go nodes 5000" --go2 go --pgn $(PGN) \
	  build/host/chess_text build/host/chess_text board:build/mega65/chess_text.bin

# A release (doc/RELEASE.md): the archive nice_chess-VERSION.tar.gz,
# whose directory holds the MEGA65 builds, nice_chess-VERSION.bin the
# windowed front end and nice_chess-text-VERSION.bin the text front
# end, the builds for niceemu, with -niceemu after nice_chess and
# nice_chess-text, which do not run under the monitor, the static
# host build without asserts as nice_chess, a UCI engine for x86_64
# Linux, the minimal build for the Nexys4DDR as
# nexys/nice_chess-nexys-VERSION.bin, and README.md, RELEASE_NOTES.md,
# and LICENSE; and SHA256SUMS.
# The release workflow runs it with VERSION set to the tag, after the
# tests and make size.
DIST = build/dist
dist: build/mega65/chess_text.bin build/mega65/chess_screen.bin build/nice/chess_text.bin \
      build/nice/chess_screen.bin build/nexys/chess_mini.bin build/host/chess_fast
	rm -rf $(DIST)
	mkdir -p $(DIST)/nice_chess-$(VERSION)/mega65 $(DIST)/nice_chess-$(VERSION)/niceemu \
	  $(DIST)/nice_chess-$(VERSION)/linux-x86_64 $(DIST)/nice_chess-$(VERSION)/nexys
	cp build/mega65/chess_screen.bin $(DIST)/nice_chess-$(VERSION)/mega65/nice_chess-$(VERSION).bin
	cp build/mega65/chess_text.bin $(DIST)/nice_chess-$(VERSION)/mega65/nice_chess-text-$(VERSION).bin
	cp build/nice/chess_screen.bin $(DIST)/nice_chess-$(VERSION)/niceemu/nice_chess-niceemu-$(VERSION).bin
	cp build/nice/chess_text.bin $(DIST)/nice_chess-$(VERSION)/niceemu/nice_chess-text-niceemu-$(VERSION).bin
	cp build/nexys/chess_mini.bin $(DIST)/nice_chess-$(VERSION)/nexys/nice_chess-nexys-$(VERSION).bin
	cp build/host/chess_fast $(DIST)/nice_chess-$(VERSION)/linux-x86_64/nice_chess
	cp README.md RELEASE_NOTES.md LICENSE $(DIST)/nice_chess-$(VERSION)/
	tar czf $(DIST)/nice_chess-$(VERSION).tar.gz -C $(DIST) --owner=0 --group=0 nice_chess-$(VERSION)
	cd $(DIST) && sha256sum nice_chess-$(VERSION).tar.gz > SHA256SUMS
	@echo "$(DIST)/nice_chess-$(VERSION).tar.gz"

# The gates of a release on the board, and their stamp (tools/release.py).
release-check:
	$(PYTHON) tools/release.py check $(BOARDLINK)

deps: $(NICE_CPU) $(NICEEMU) $(PERFTSUITE) $(SEARCHSUITE)

deps/nice_cpu-$(NICE_CPU_TAG):
	git clone --quiet --depth 1 --branch $(NICE_CPU_TAG) -c advice.detachedHead=false \
	  https://github.com/MJoergen/nice_cpu $@

deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu:
	@mkdir -p deps
	curl -sSfL -o deps/$(NICEEMU_TGZ) $(RELEASE_URL)/$(NICEEMU_TGZ)
	echo "$(NICEEMU_SHA256)  deps/$(NICEEMU_TGZ)" | sha256sum -c --quiet
	tar xzf deps/$(NICEEMU_TGZ) -C deps
	rm deps/$(NICEEMU_TGZ)
	@touch $@

$(PERFTSUITE):
	@mkdir -p $(@D)
	curl -sSfL -o $@.tmp $(MCHESS2_RAW)/tests/perftsuite.epd
	echo "$(PERFTSUITE_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

$(SEARCHSUITE):
	@mkdir -p $(@D)
	curl -sSfL -o $@.tmp $(MCHESS2_RAW)/tests/searchsuite.epd
	echo "$(SEARCHSUITE_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

deps/books/8moves_v3.pgn:
	@mkdir -p $(@D)
	curl -sSfL -o $@.zip https://raw.githubusercontent.com/official-stockfish/books/$(BOOKS_COMMIT)/8moves_v3.pgn.zip
	echo "$(BOOK_ZIP_SHA256)  $@.zip" | sha256sum -c --quiet
	$(PYTHON) -m zipfile -e $@.zip $(@D)
	rm $@.zip
	@touch $@

clean:
	rm -rf build

distclean: clean
	rm -rf deps
