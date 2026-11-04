# nice_chess: a chess engine for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag, and
# the perft suite from mchess2 at a pinned commit (doc/HISTORY.md,
# Decisions). make deps fetches them into deps/. NICE_CPU=DIR and
# NICEEMU=PATH use local copies of nice_cpu and niceemu instead.
# make help lists the targets.

NICE_CPU_TAG    := V1.2
NICEEMU_SHA256  := cb498a974cec1ae1f2e56441e6bb1f3ec20d593bb505ff32a566b7f4eade6e94
MCHESS2_COMMIT  := 65e88554b20e06079301ef2314f6559fab06d062
PERFTSUITE_SHA256 := 363e5205dfff3205fc50b376fb1770f9156464456282372c8c0a0e5b45a0bf07
SEARCHSUITE_SHA256 := 291bd3110550fbb26f8c887e4ed23249200d044b52d912b573b1875351b83b0a
# The openings of make rate: 8moves_v3.pgn of fishtest's books.
BOOKS_COMMIT    := 2ec69181c0b1367c5c58bcf5304a79c7b198b655
BOOK_ZIP_SHA256 := 7e1e9dd118b4bb97d8a8b5b8a790c86e21f8509d59a27d2883767d94477be02e

NICE_CPU ?= deps/nice_cpu
NICEEMU  ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
NICEMON  ?= ../NiceMonMEGA65
PYTHON   ?= python3
CC       ?= cc
export NICEEMU NICEMON

# How to reach the board: ETH=IP or PORT=DEVICE, as NiceMonMEGA65's tools
# take them. Only the board targets need one.
BOARDLINK = $(if $(ETH),--eth $(ETH),$(if $(PORT),--port $(PORT),$(error ETH=IP or PORT=DEVICE names the board)))
LEVEL ?= 2

NICEBUILD  = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz
PERFTSUITE  = deps/mchess2/perftsuite.epd
SEARCHSUITE = deps/mchess2/searchsuite.epd
MCHESS2_RAW = https://raw.githubusercontent.com/MJoergen/mchess2/$(MCHESS2_COMMIT)

# mchess2's sources, to build it as an opponent (make match-mchess2)
MCHESS2_SRC = main.cc CBoard.cc CMove.cc ai.cc CMoveList.cc CPerft.cc \
	CHashEntry.cc CHashTable.cc CPerftSuite.cc CSearchSuite.cc \
	CBoard.h CHashEntry.h CHashTable.h CMove.h CMoveList.h CPerft.h \
	CPerftSuite.h CSearchSuite.h CSquare.h CTime.h ai.h crc32.h trace.h

# The MEGA65 gives a program under the monitor the words 0x8000 to
# 0x1EFFF, 92K, for its image, its linear memory, and the NICE stack,
# which the monitor's routines share and use at most 32 words of
# (NiceMonMEGA65 V1.2.1). The Nexys4DDR would give 28K.
BUDGET = 94176

# The engine, and the table of its opening book, which tools/mkbook.c
# makes from src/engine/book.txt with the engine's own keys.
BOOKDATA = build/gen/book_data.c
CORE = $(wildcard src/engine/*.c)
ENGINE = $(CORE) $(BOOKDATA)
# The headers, and the Makefile, whose flags every build takes, so that
# a change of the flags rebuilds.
HEADERS = $(wildcard src/engine/*.h) src/platform/platform.h Makefile

# NICE: optimized for speed, and without asserts. The engine keeps its
# move lists in its own data, so the wasm stack is small. The memory is
# three pages, 192 KB, for the transposition table's 128 KB (TT_BITS).
NICEFLAGS = -O 2 -D NDEBUG --stack 8192 --memory 196608 --symbols --keep
# The MEGA65: the same, built for the monitor, which loads it at 0x8000.
MONFLAGS = $(NICEFLAGS) --monitor

# The host: with asserts, and a check of the Zobrist key and the
# evaluation's sums at every node of perft.
HOSTFLAGS = -O2 -W -Wall -Wextra -DCHECK_STATE
# The host, for matches: without asserts and the check, which play the
# same moves, only faster.
FASTFLAGS = -O2 -W -Wall -Wextra -DNDEBUG

# A rating, by tools/rate.sh: cutechess-cli and Ordo, and the opponents.
# The board's search at 5 seconds a move is about 93000 nodes (LOG.md).
CUTECHESS ?= cutechess-cli
ORDO      ?= ordo
STOCKFISH ?= stockfish
NODES     ?= 93000
ELOS      ?= 1700 1900 2100
GAMES     ?= 200
CONCURRENCY ?= 4
BOOK      ?= deps/books/8moves_v3.pgn
export CUTECHESS ORDO STOCKFISH NODES ELOS GAMES CONCURRENCY BOOK

.PHONY: help all test host-test nice-test perft-deep screen-game match-random match-mchess2 rate openings size deps clean distclean
.PHONY: board-text board-screen board-test board-game board-match board-stockfish
help:
	@echo "nice_chess: a chess engine for the NICE system"
	@echo ""
	@echo "  make help          show this text (the default)"
	@echo "  make deps          fetch nice_cpu $(NICE_CPU_TAG), niceemu $(NICE_CPU_TAG), and mchess2's test suites into deps/"
	@echo "  make all           build both front ends, for niceemu, for the MEGA65, and for the host"
	@echo "  make test          the tests, on the host and in niceemu"
	@echo "  make perft-deep    the perft suite to depth 5 on the host (a minute)"
	@echo "  make screen-game   a full game in the windowed front end, in niceemu (a minute)"
	@echo "  make match-random  the host build against random moves (20 games)"
	@echo "  make match-mchess2 the host build against mchess2 (20 games, 15 minutes)"
	@echo "  make openings      fetch the openings of make rate, 8moves_v3.pgn, into deps/books/"
	@echo "  make rate          a rating: cutechess-cli games against Stockfish's UCI_Elo levels, and Ordo"
	@echo "  make size          the size of the MEGA65 builds, against the board's budget"
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
	@echo "  make board-stockfish  a game on the board's screen against Stockfish (ELO=1800, BLACK=1: it is black)"
	@echo ""
	@echo "NICE_CPU=DIR and NICEEMU=PATH use local copies of nice_cpu and niceemu."

all: build/nice/chess_text.bin build/nice/chess_screen.bin build/host/chess_text \
     build/mega65/chess_text.bin build/mega65/chess_screen.bin

build/nice/chess_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) src/ui/text.c src/platform/nice.c

build/nice/chess_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/platform/nice.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) src/ui/screen.c src/platform/nice.c

build/mega65/chess_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/nice.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) -o $@ $(ENGINE) src/ui/text.c src/platform/nice.c

build/mega65/chess_screen.bin: $(ENGINE) $(HEADERS) src/ui/screen.c src/platform/nice.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(MONFLAGS) -o $@ $(ENGINE) src/ui/screen.c src/platform/nice.c

# nice_cpu's stand-in for a monitor at 0, which runs a program at 0x8000
# as the monitor does, so that the MEGA65 builds run in niceemu too,
# joined after it (a .bin file is address and word pairs).
build/nice/monstub.bin: $(NICE_CPU)/runtime/test/monitor.nasm | $(NICE_CPU)
	@mkdir -p $(@D)
	$(PYTHON) $(NICE_CPU)/assembler/niceasm.py -o $@ $<

build/test/mon_%.bin: build/nice/monstub.bin build/mega65/%.bin
	@mkdir -p $(@D)
	cat $^ > $@

build/nice/perftsuite.bin: $(ENGINE) $(HEADERS) tests/perftsuite.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -D PERFT_MAX_DEPTH=3 -o $@ $(ENGINE) tests/perftsuite.c

build/host/mkbook: $(CORE) $(HEADERS) tools/mkbook.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(CORE) tools/mkbook.c

$(BOOKDATA): build/host/mkbook src/engine/book.txt
	@mkdir -p $(@D)
	build/host/mkbook < src/engine/book.txt > $@.tmp
	mv $@.tmp $@

build/host/chess_text: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/host.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) src/ui/text.c src/platform/host.c

build/host/chess_fast: $(ENGINE) $(HEADERS) src/ui/text.c src/platform/host.c
	@mkdir -p $(@D)
	$(CC) $(FASTFLAGS) -o $@ $(ENGINE) src/ui/text.c src/platform/host.c

build/host/perftsuite: $(ENGINE) $(HEADERS) tests/perftsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/perftsuite.c

build/host/searchsuite: $(ENGINE) $(HEADERS) tests/searchsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/searchsuite.c

test: host-test nice-test

host-test: build/host/chess_text build/host/perftsuite build/host/searchsuite $(PERFTSUITE) $(SEARCHSUITE)
	@tools/runtest.sh host_text text tests/text.input host build/host/chess_text
	@tools/runtest.sh host_search search tests/search.input host build/host/chess_text
	@tools/runtest.sh host_book book tests/book.input host build/host/chess_text
	@tools/runtest.sh host_perftsuite4 perftsuite4 $(PERFTSUITE) host build/host/perftsuite 4
	@tools/runtest.sh host_searchsuite searchsuite $(SEARCHSUITE) host build/host/searchsuite 10000

nice-test: build/nice/chess_text.bin build/nice/chess_screen.bin build/nice/perftsuite.bin $(PERFTSUITE) \
           build/test/mon_chess_text.bin build/test/mon_chess_screen.bin | $(NICEEMU)
	@tools/runtest.sh nice_screen screen tests/screen.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_screen_book screen_book tests/screen_book.keys screen build/nice/chess_screen.bin
	@tools/runtest.sh nice_text text tests/text.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_search search tests/search.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_book book tests/book.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_perftsuite3 perftsuite3 $(PERFTSUITE) nice build/nice/perftsuite.bin
	@tools/runtest.sh mega65_screen screen tests/screen.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_screen_book screen_book tests/screen_book.keys screen build/test/mon_chess_screen.bin
	@tools/runtest.sh mega65_text text tests/text.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh mega65_search search tests/search.input nice build/test/mon_chess_text.bin
	@tools/runtest.sh mega65_book book tests/book.input nice build/test/mon_chess_text.bin

perft-deep: build/host/perftsuite $(PERFTSUITE)
	@tools/runtest.sh host_perftsuite5 perftsuite5 $(PERFTSUITE) host build/host/perftsuite 5

# The keys of a game that tools/screen_game.py played, to checkmate.
screen-game: build/nice/chess_screen.bin | $(NICEEMU)
	@tools/runtest.sh nice_screen_game screen_game tests/screen_game.keys screen build/nice/chess_screen.bin

# Against random moves, at 20000 nodes a move: it must win every game.
match-random: build/host/chess_text
	$(PYTHON) tools/match.py --go1 "go nodes 20000" --min-score 20 \
	  build/host/chess_text build/host/chess_text random

# Against mchess2, at 40 moves in 20 seconds each.
match-mchess2: build/host/chess_text build/host/mchess2
	$(PYTHON) tools/match.py --time 20 build/host/chess_text build/host/chess_text build/host/mchess2

# A rating of the board's engine at 5 seconds a move (tools/rate.sh).
rate: build/host/chess_fast $(BOOK)
	tools/rate.sh

openings: deps/books/8moves_v3.pgn

build/host/mchess2: $(addprefix deps/mchess2/src/,$(MCHESS2_SRC))
	@mkdir -p $(@D)
	$(CXX) -O2 -DNAME=mchess2 -o $@ $(filter %.cc,$^)

deps/mchess2/src/%:
	@mkdir -p $(@D)
	curl -sSfL -o $@ $(MCHESS2_RAW)/$*

size: build/mega65/chess_text.bin build/mega65/chess_screen.bin build/test/mon_chess_text.bin build/test/mon_chess_screen.bin | $(NICEEMU)
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size.input --run build/test/mon_chess_text.bin \
	  --budget $(BUDGET) --check build/mega65/chess_text.bin
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/screen.keys --run build/test/mon_chess_screen.bin \
	  --budget $(BUDGET) --check build/mega65/chess_screen.bin

# The board. load.py loads a program under the monitor and types r, which
# runs it; what it prints then comes on the line, such as nc IP 4510 or
# picocom -b 2000000 DEVICE (NiceMonMEGA65's tools/README.md).
board-text: build/mega65/chess_text.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

board-screen: build/mega65/chess_screen.bin
	$(PYTHON) $(NICEMON)/tools/load.py $(BOARDLINK) --monitor $<

board-test: build/mega65/chess_text.bin build/mega65/chess_screen.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_text text tests/text.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_search search tests/search.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_book book tests/book.input board build/mega65/chess_text.bin
	@BOARDLINK="$(BOARDLINK)" tools/runtest.sh board_screen screen tests/screen.keys board-screen build/mega65/chess_screen.bin

# A game at LEVEL (2 by default) by the keys, white's moves from the host
# build; the keys, and the screen at the end, go to build/mega65/.
# A game in the windowed front end on the board, against Stockfish at
# UCI_Elo $(ELO) and a second a move, white unless BLACK=1; the board's
# screen shows it as it is played. The engine has its level 3, 5 seconds.
# The host prints each move with Stockfish's score, at full strength.
ELO ?= 1800
board-stockfish: build/mega65/chess_screen.bin build/host/chess_text
	$(PYTHON) tools/screen_game.py $(BOARDLINK) --level 3 --opponent "$(STOCKFISH)" \
	  --opt UCI_LimitStrength=true --opt UCI_Elo=$(ELO) --movetime 1000 $(if $(BLACK),--black) \
	  --eval "$(STOCKFISH)" \
	  build/mega65/chess_screen.bin build/host/chess_text build/mega65/stockfish.keys

board-game: build/mega65/chess_screen.bin build/host/chess_text
	$(PYTHON) tools/screen_game.py $(BOARDLINK) --level $(LEVEL) --dump build/mega65/game.dump \
	  build/mega65/chess_screen.bin build/host/chess_text build/mega65/game.keys

# Two games, one with each color, the board at its default time of a
# move, 5 seconds, and the host build at 5000 nodes a move.
board-match: build/mega65/chess_text.bin build/host/chess_text
	$(PYTHON) tools/match.py $(BOARDLINK) --openings 1 --go1 "go nodes 5000" --go2 go \
	  build/host/chess_text build/host/chess_text board:build/mega65/chess_text.bin

deps: $(NICE_CPU) $(NICEEMU) $(PERFTSUITE) $(SEARCHSUITE)

deps/nice_cpu:
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
