# nice_chess: a chess engine for the NICE system.
#
# The NICE toolchain comes from nice_cpu at a pinned release tag
# (doc/PORTING_PLAN.md, Decisions). make deps clones it into deps/ and
# downloads niceemu of the same release; NICE_CPU=DIR and NICEEMU=PATH
# use local copies instead, and then nothing is fetched.
#
#   make          build build/mchess.bin
#   make test     build and run the tests in niceemu
#   make clean    remove build/
#   make distclean  remove build/ and deps/

NICE_CPU_TAG := V1.2
NICEEMU_SHA256 := cb498a974cec1ae1f2e56441e6bb1f3ec20d593bb505ff32a566b7f4eade6e94

NICE_CPU ?= deps/nice_cpu
NICEEMU ?= deps/niceemu-$(NICE_CPU_TAG)-x86_64-linux/niceemu
PYTHON ?= python3

NICEBUILD = $(PYTHON) $(NICE_CPU)/translator/nicebuild.py
RELEASE_URL = https://github.com/MJoergen/nice_cpu/releases/download/$(NICE_CPU_TAG)
NICEEMU_TGZ = niceemu-$(NICE_CPU_TAG)-x86_64-linux.tar.gz

# mchess.c keeps a move list of 2.5 KB on the stack in each level of its
# search, so it links as in nice_cpu: a 128 KB stack in four pages.
MCHESS_FLAGS = --stack 131072 --memory 262144

# Each test is tests/X.input, the console input of a program, and
# tests/X.expected, the lines of its output that matter: those that
# match tests/X.grep, then R1 (main's result) and the exit status of
# niceemu. TESTS lists them as PROGRAM:TEST.
TESTS = mchess:perft3

.PHONY: all test deps clean distclean
all: build/mchess.bin

build/mchess.bin: src/mchess.c | $(NICE_CPU)
	@mkdir -p build
	$(NICEBUILD) $(MCHESS_FLAGS) -o $@ $<

test: $(foreach t,$(TESTS),build/$(word 1,$(subst :, ,$(t))).bin) | $(NICEEMU)
	@fail=0; for t in $(TESTS); do \
	  prog=$${t%%:*}; name=$${t#*:}; \
	  $(NICEEMU) -i tests/$$name.input build/$$prog.bin < /dev/null > build/$$name.raw; \
	  status=$$?; \
	  { grep -f tests/$$name.grep build/$$name.raw; \
	    grep -o 'R1  [0-9a-f]*' build/$$name.raw; \
	    echo "exit $$status"; } > build/$$name.out; \
	  if cmp -s build/$$name.out tests/$$name.expected; then echo "PASS $$name"; \
	  else echo "FAIL $$name"; diff tests/$$name.expected build/$$name.out; fail=1; fi; \
	done; exit $$fail

deps: $(NICE_CPU) $(NICEEMU)

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

clean:
	rm -rf build

distclean: clean
	rm -rf deps
