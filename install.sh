#!/bin/sh
# Install Wire on Linux or macOS from the latest GitHub release.
#
#   curl -fsSL https://raw.githubusercontent.com/Husseinabdulameer11/wire/main/install.sh | sh
#
# Options, as environment variables:
#   WIRE_VERSION=0.1.0          install that release instead of the latest
#   WIRE_PREFIX=/some/prefix    where to install (default ~/.local)
#
#   ... | sh -s -- --uninstall  remove it again
#
# What it does: downloads wire-<os>-<arch>.tar.gz and SHA256SUMS from the
# release, checks the checksum, and puts
#   wire                 in <prefix>/bin
#   the standard library in <prefix>/share/wire/lib (wire finds it there)
#   the examples         in <prefix>/share/wire/examples
# Nothing outside your home folder is touched unless you set WIRE_PREFIX there.
set -eu

WIRE_REPO=${WIRE_REPO:-Husseinabdulameer11/wire}

# Everything is inside main, so a download cut off half way runs nothing.
main() {
    prefix=${WIRE_PREFIX:-$HOME/.local}
    bindir=$prefix/bin
    sharedir=$prefix/share/wire      # wire looks for ../share/wire/lib

    if [ "${1:-}" = "--uninstall" ]; then
        rm -f "$bindir/wire"
        rm -rf "$sharedir"
        say "removed $bindir/wire and $sharedir"
        exit 0
    fi

    case $(uname -s) in
        Linux)  os=linux ;;
        Darwin) os=macos ;;
        MINGW*|MSYS*|CYGWIN*)
            die "on Windows, use the installer (wire-windows-setup.exe) from the releases page" ;;
        *) die "no prebuilt Wire for $(uname -s); build it from source (see README)" ;;
    esac
    case $(uname -m) in
        x86_64|amd64)  arch=x86_64 ;;
        aarch64|arm64) arch=aarch64 ;;
        *) die "no prebuilt Wire for $(uname -m); build it from source (see README)" ;;
    esac
    [ "$os" = macos ] && arch=universal     # one binary for Intel and Apple silicon

    name=wire-$os-$arch
    if [ -n "${WIRE_DOWNLOAD_BASE:-}" ]; then
        base=$WIRE_DOWNLOAD_BASE                         # mirrors and testing
    elif [ -n "${WIRE_VERSION:-}" ]; then
        base=https://github.com/$WIRE_REPO/releases/download/v${WIRE_VERSION#v}
    else
        base=https://github.com/$WIRE_REPO/releases/latest/download
    fi

    tmp=$(mktemp -d 2>/dev/null || mktemp -d -t wire)
    trap 'rm -rf "$tmp"' EXIT INT TERM

    say "downloading $name.tar.gz"
    fetch "$base/$name.tar.gz" "$tmp/$name.tar.gz" ||
        die "download failed: $base/$name.tar.gz"

    if fetch "$base/SHA256SUMS" "$tmp/SHA256SUMS" 2>/dev/null; then
        want=$(grep " $name.tar.gz\$" "$tmp/SHA256SUMS" | cut -d' ' -f1)
        got=$(sha256 "$tmp/$name.tar.gz")
        if [ -z "$want" ]; then
            die "$name.tar.gz is not listed in SHA256SUMS"
        elif [ -z "$got" ]; then
            say "warning: no sha256sum or shasum found, checksum not verified"
        elif [ "$want" != "$got" ]; then
            die "checksum mismatch for $name.tar.gz (download corrupted?)"
        else
            say "checksum ok"
        fi
    else
        say "warning: no SHA256SUMS in this release, checksum not verified"
    fi

    tar -xzf "$tmp/$name.tar.gz" -C "$tmp"
    [ -f "$tmp/$name/bin/wire" ] || die "the archive has no wire binary"

    mkdir -p "$bindir" "$sharedir"
    rm -f "$bindir/wire"
    cp "$tmp/$name/bin/wire" "$bindir/wire"
    chmod 755 "$bindir/wire"
    rm -rf "$sharedir/lib" "$sharedir/examples"
    cp -R "$tmp/$name/lib" "$sharedir/lib"
    cp -R "$tmp/$name/examples" "$sharedir/examples"
    cp "$tmp/$name/README.md" "$sharedir/README.md"
    if [ "$os" = macos ]; then
        xattr -d com.apple.quarantine "$bindir/wire" 2>/dev/null || true
    fi

    say "installed $("$bindir/wire" --version) to $bindir/wire"
    say "examples are in $sharedir/examples"

    case ":$PATH:" in
        *":$bindir:"*)
            say ""
            say "try:  wire -o full_adder.html $sharedir/examples/notes/full_adder.wire"
            say "      and open full_adder.html in your browser" ;;
        *)
            say ""
            say "$bindir is not on your PATH yet. Add it with:"
            case ${SHELL:-} in
                */zsh)  say "  echo 'export PATH=\"$bindir:\$PATH\"' >> ~/.zshrc && exec zsh" ;;
                */fish) say "  fish_add_path $bindir" ;;
                *)      say "  echo 'export PATH=\"$bindir:\$PATH\"' >> ~/.bashrc && exec bash" ;;
            esac
            say "then:  wire -o full_adder.html $sharedir/examples/notes/full_adder.wire" ;;
    esac
}

say() { printf '%s\n' "$*"; }
die() { printf 'wire install: %s\n' "$*" >&2; exit 1; }

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$1" -O "$2"
    else
        die "needs curl or wget"
    fi
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

main "$@"
