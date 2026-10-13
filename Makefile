# Wire - describe a digital circuit as text, get an interactive page.
# Works with GNU make on Linux, macOS and Windows (MSYS2).
#
# Wire is built with gram, a separate parser library and generator
# (gramgen turns src/wire.gram into rule ids, and the parser is linked in).
# Get gram and do ONE of these:
#
#   1. install it:        cd gram && make install          (or PREFIX=~/.local)
#                         then just `make` here; gramgen is found on PATH
#   2. keep a checkout next to this one, as ../gram         (found automatically)
#   3. point at one:      make GRAM=path/to/gram
#                    or:  make GRAM_PREFIX=/where/gram/is/installed
CFLAGS  ?= -O2
LDFLAGS ?=
VERSION ?= dev
PREFIX  ?= /usr/local

ifeq ($(OS),Windows_NT)
  EXE := .exe
  ifeq ($(origin CC),default)
    CC := gcc
  endif
else
  EXE :=
endif

# ---- find gram --------------------------------------------------------------
ifeq ($(GRAM)$(GRAM_PREFIX),)
  GRAMGEN_ON_PATH := $(shell command -v gramgen 2>/dev/null)
  ifneq ($(GRAMGEN_ON_PATH),)
    # .../bin/gramgen -> ...
    GRAM_PREFIX := $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(GRAMGEN_ON_PATH)))))
  else ifneq ($(wildcard ../gram/gram.c),)
    GRAM := ../gram
  endif
endif

ifneq ($(GRAM),)
  # a gram source checkout: build its library and gramgen into build/
  GRAMGEN  = build/gramgen$(EXE)
  GRAM_LIB = build/gram.o
  GRAM_INC = -I$(GRAM)
  GRAM_FROM = source checkout $(GRAM)
else ifneq ($(GRAM_PREFIX),)
  # an installed gram (make install)
  GRAMGEN  = $(GRAM_PREFIX)/bin/gramgen$(EXE)
  GRAM_LIB = $(GRAM_PREFIX)/lib/libgram.a
  GRAM_INC = -I$(GRAM_PREFIX)/include
  GRAM_FROM = installed in $(GRAM_PREFIX)
else
  GRAM_MISSING = 1
endif
# -----------------------------------------------------------------------------

WIRE  = build/wire$(EXE)
EMBED = build/embed$(EXE)
SRC   = src/wire.c

all: $(WIRE)

build:
	mkdir -p build

need-gram:
ifdef GRAM_MISSING
	@echo "wire: gram was not found. Wire is built with the gram parser library."
	@echo "      Get gram, then either run 'make install' in it, put it next to"
	@echo "      this folder as ../gram, or run: make GRAM=path/to/gram"
	@exit 1
else
	@echo "wire: using gram ($(GRAM_FROM))"
endif

ifneq ($(GRAM),)
build/gram.o: $(GRAM)/gram.c $(GRAM)/gram.h | build
	$(CC) $(CFLAGS) -c $(GRAM)/gram.c -o $@

$(GRAMGEN): $(GRAM)/tools/gramgen.c build/gram.o
	$(CC) $(CFLAGS) $(GRAM)/tools/gramgen.c build/gram.o -o $@
endif

$(EMBED): tools/embed.c | build
	$(CC) $(CFLAGS) -Wall -Wextra tools/embed.c -o $@

# grammar -> rule ids and grammar text (build/wire_grammar.h)
build/wire_grammar.h: src/wire.gram $(GRAMGEN) | need-gram build
	$(GRAMGEN) src/wire.gram wire build

# the HTML viewer, baked into the binary as a string
build/wire_view.h: src/wire_view.html src/wire_sim.js $(EMBED)
	$(EMBED) src/wire_view.html wire_view > $@

$(WIRE): $(SRC) build/wire_grammar.h build/wire_view.h $(GRAM_LIB) | need-gram
	$(CC) $(CFLAGS) -Wall -Wextra -DWIRE_VERSION='"$(VERSION)"' -Isrc -Ibuild $(GRAM_INC) \
	    $(SRC) $(GRAM_LIB) $(LDFLAGS) -o $@

test: $(WIRE)
	@for f in examples/*.wire; do $(WIRE) -o build/$$(basename $$f .wire).html $$f || exit 1; done
	@for f in tests/errors/*.wire; do \
	    if $(WIRE) --check $$f 2> build/err.txt; then echo "$$f: expected an error"; exit 1; fi; \
	    grep -qF "$$(sed -n 's/^# expect: //p' $$f)" build/err.txt || { echo "$$f: wrong error:"; cat build/err.txt; exit 1; }; \
	done
	@if command -v node >/dev/null 2>&1; then node tests/sim/simtest.js $(WIRE) || exit 1; \
	   node tests/sim/run.js $(WIRE) examples/sap1.wire 2000 out | grep -q "out = 87" \
	     || { echo "examples/sap1.wire: the SAP-1 did not compute 3 x 29 = 87"; exit 1; }; \
	   node examples/sap/microcode.js --check || exit 1; \
	   node tests/sap/saptest.js $(WIRE) --random 100 || exit 1; \
	 else echo "wire: node not found, skipping the simulator tests (tests/sim)"; fi
	@echo "wire: all tests passed"

# every example as a page, plus an index, in build/site: drag that folder
# onto Netlify Drop (app.netlify.com/drop), or serve it from any static host
# with a book to read beside the circuits: BOOK=books/x.pdf TOPICS=books/x.topics
BOOK   ?= $(wildcard books/fpga_summary.pdf)
TOPICS ?= $(wildcard books/fpga_summary.topics)
site: $(WIRE)
	node tools/site.js $(WIRE) build/site $(if $(BOOK),--book $(BOOK)) $(if $(TOPICS),--topics $(TOPICS))

# Linux/macOS: installs build/wire as $(PREFIX)/bin/wire
install: $(WIRE)
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/wire/lib
	cp $(WIRE) $(DESTDIR)$(PREFIX)/bin/
	cp lib/*.wire $(DESTDIR)$(PREFIX)/share/wire/lib/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wire$(EXE)
	rm -rf $(DESTDIR)$(PREFIX)/share/wire

clean:
	rm -rf build

.PHONY: all test install uninstall clean need-gram site
