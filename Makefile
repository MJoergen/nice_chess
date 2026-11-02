# nice_chess: a chess engine for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag, and
# the perft suite from mchess2 at a pinned commit (doc/PORTING_PLAN.md,
# Decisions). make deps fetches them into deps/. NICE_CPU=DIR and
# NICEEMU=PATH use local copies of nice_cpu and niceemu instead.
#
#   make             build the text front end, for NICE and for the host
#   make test        the tests, on the host and in niceemu
#   make perft-deep  the perft suite to depth 5 on the host (a minute)
#   make size        the size of the NICE build, against the board's budget
#   make clean       remove build/
#   make distclean   remove build/ and deps/

NICE_CPU_TAG    := V1.2
NICEEMU_SHA256  := cb498a974cec1ae1f2e56441e6bb1f3ec20d593bb505ff32a566b7f4eade6e94
MCHESS2_COMMIT  := 65e88554b20e06079301ef2314f6559fab06d062
PERFTSUITE_SHA256 := 363e5205dfff3205fc50b376fb1770f9156464456282372c8c0a0e5b45a0bf07

NICE_CPU ?= deps/nice_cpu
NICEEMU  ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
PYTHON   ?= python3
CC       ?= cc
export NICEEMU

NICEBUILD  = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz
PERFTSUITE  = deps/mchess2/perftsuite.epd

# The board gives a program under the monitor 28K words, for its image,
# its linear memory, and the NICE stack (doc/PORTING_PLAN.md, Memory).
BUDGET = 28672

ENGINE = $(wildcard src/engine/*.c)
HEADERS = $(wildcard src/engine/*.h)

# NICE: optimized for size, which costs about 20% in speed, and without
# asserts. The engine keeps its move lists in its own data, so the wasm
# stack is small, and all of the memory is one page.
NICEFLAGS = -O s -D NDEBUG --stack 8192 --memory 65536 --symbols --keep

# The host: with asserts, and a check of the Zobrist key at every node
# of perft.
HOSTFLAGS = -O2 -W -Wall -Wextra -DCHECK_HASH

.PHONY: all test host-test nice-test perft-deep size deps clean distclean
all: build/nice/chess_text.bin build/host/chess_text

build/nice/chess_text.bin: $(ENGINE) $(HEADERS) src/ui/text.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -o $@ $(ENGINE) src/ui/text.c

build/nice/perftsuite.bin: $(ENGINE) $(HEADERS) tests/perftsuite.c | $(NICE_CPU)
	@mkdir -p $(@D)
	$(NICEBUILD) $(NICEFLAGS) -D PERFT_MAX_DEPTH=3 -o $@ $(ENGINE) tests/perftsuite.c

build/host/chess_text: $(ENGINE) $(HEADERS) src/ui/text.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) src/ui/text.c

build/host/perftsuite: $(ENGINE) $(HEADERS) tests/perftsuite.c
	@mkdir -p $(@D)
	$(CC) $(HOSTFLAGS) -o $@ $(ENGINE) tests/perftsuite.c

test: host-test nice-test

host-test: build/host/chess_text build/host/perftsuite $(PERFTSUITE)
	@tools/runtest.sh host_text text tests/text.input host build/host/chess_text
	@tools/runtest.sh host_perftsuite4 perftsuite4 $(PERFTSUITE) host build/host/perftsuite 4

nice-test: build/nice/chess_text.bin build/nice/perftsuite.bin $(PERFTSUITE) | $(NICEEMU)
	@tools/runtest.sh nice_text text tests/text.input nice build/nice/chess_text.bin
	@tools/runtest.sh nice_perftsuite3 perftsuite3 $(PERFTSUITE) nice build/nice/perftsuite.bin

perft-deep: build/host/perftsuite $(PERFTSUITE)
	@tools/runtest.sh host_perftsuite5 perftsuite5 $(PERFTSUITE) host build/host/perftsuite 5

size: build/nice/chess_text.bin | $(NICEEMU)
	$(PYTHON) tools/size.py --emu $(NICEEMU) --input tests/size.input --budget $(BUDGET) $<

deps: $(NICE_CPU) $(NICEEMU) $(PERFTSUITE)

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
	curl -sSfL -o $@.tmp \
	  https://raw.githubusercontent.com/MJoergen/mchess2/$(MCHESS2_COMMIT)/tests/perftsuite.epd
	echo "$(PERFTSUITE_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

clean:
	rm -rf build

distclean: clean
	rm -rf deps
