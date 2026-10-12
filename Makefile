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
build/wire_view.h: src/wire_view.html $(EMBED)
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
	@echo "wire: all tests passed"

# Linux/macOS: installs build/wire as $(PREFIX)/bin/wire
install: $(WIRE)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(WIRE) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wire$(EXE)

clean:
	rm -rf build

.PHONY: all test install uninstall clean need-gram
