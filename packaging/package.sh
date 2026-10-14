#!/bin/sh
# Pack a built Wire into a release archive.
#
#   packaging/package.sh <target>      e.g. linux-x86_64, macos-universal,
#                                           windows-x86_64
#
# Run from the wire folder after `make`. Writes dist/wire-<target>.tar.gz
# (dist/wire-<target>.zip for windows-*) holding one folder:
#
#   wire-<target>/bin/wire[.exe]
#                 lib/          the standard library (wire finds it at ../lib)
#                 examples/     every example, each with its VHDL
#                 README.md  LICENSE
#
# The archive names carry no version so that
# https://github.com/<owner>/wire/releases/latest/download/<name> always
# points at the newest release (install.sh relies on that).
set -eu

target=${1:?usage: packaging/package.sh <target>}
name=wire-$target
exe=build/wire
case $target in windows-*) exe=build/wire.exe ;; esac

[ -f "$exe" ] || { echo "package.sh: $exe not found, run make first" >&2; exit 1; }

rm -rf "dist/$name"
mkdir -p "dist/$name/bin"
cp "$exe" "dist/$name/bin/"
cp README.md LICENSE "dist/$name/"
cp -R lib "dist/$name/lib"
cp -R examples "dist/$name/examples"

cd dist
case $target in
    windows-*) rm -f "$name.zip";    zip -qr "$name.zip" "$name";       echo "wrote dist/$name.zip" ;;
    *)         rm -f "$name.tar.gz"; tar -czf "$name.tar.gz" "$name";   echo "wrote dist/$name.tar.gz" ;;
esac
