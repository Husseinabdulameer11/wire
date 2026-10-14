#!/bin/sh
# Runs once when the dev container is created (postCreateCommand).
# Gets gram, installs it into ~/.local, then builds, tests and installs Wire.
set -eu

GRAM_REPO=${GRAM_REPO:-https://github.com/Husseinabdulameer11/gram.git}
GRAM_REF=${GRAM_REF:-main}
PREFIX=$HOME/.local
export PATH="$PREFIX/bin:$PATH"

need=""
command -v cc   >/dev/null 2>&1 || need="$need build-essential"
command -v make >/dev/null 2>&1 || need="$need make"
command -v ghdl >/dev/null 2>&1 || need="$need ghdl"     # for the VHDL checks
if [ -n "$need" ]; then
    echo "== installing$need"
    sudo apt-get update -q
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends $need ||
        echo "(could not install$need; make test skips what it cannot run)"
fi

# gram next to wire if we may write there (Codespaces: /workspaces/gram)
wire_dir=$(pwd)
parent=$(dirname "$wire_dir")
if [ -w "$parent" ]; then gram_dir=$parent/gram; else gram_dir=$HOME/gram; fi

if [ -f "$gram_dir/gram.c" ]; then
    echo "== using gram in $gram_dir"
else
    echo "== cloning gram ($GRAM_REPO) into $gram_dir"
    git clone --depth 1 --branch "$GRAM_REF" "$GRAM_REPO" "$gram_dir"
fi

echo "== installing gram into $PREFIX"
make -C "$gram_dir" install PREFIX="$PREFIX"

echo "== building, testing and installing wire"
make clean
make test GRAM_PREFIX="$PREFIX"
make install PREFIX="$PREFIX"

echo
echo "Ready. Try:  wire examples/notes/full_adder.wire"
echo "             make site && cd build/site && python3 -m http.server 8000"
echo "             (then open the forwarded port 8000)"
echo "gram is in $gram_dir. After changing src/wire.gram:  make install"
echo "After changing gram itself:  make -C $gram_dir install PREFIX=$PREFIX && make clean install"
