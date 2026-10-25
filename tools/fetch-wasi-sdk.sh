#!/usr/bin/env bash
# The pinned SDK, fetched and verified -- one version, one checksum, because
# an artifact is reproducible only against an exact release. cmake/wasi.cmake
# finds the result on its own.
#   tools/fetch-wasi-sdk.sh [install-dir]     # default: ~/wasi-sdk-34.0
set -euo pipefail

VERSION=34.0
TARBALL=wasi-sdk-${VERSION}-x86_64-linux.tar.gz
URL=https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/${TARBALL}
SHA256=b761e3a0721dbae9c09a0059e5fdb2bf917d1b4a8a7b430fb3b5aafb0984b2c4

dest=${1:-"$HOME/wasi-sdk-${VERSION}"}

sdk_complete() {
  local directory=$1 installed_version tool target
  [ -f "$directory/VERSION" ] || return 1
  IFS= read -r installed_version < "$directory/VERSION" || return 1
  [ "$installed_version" = "$VERSION" ] || return 1
  for tool in clang clang++ llvm-ar llvm-ranlib wasm-ld; do
    [ -x "$directory/bin/$tool" ] || return 1
  done
  for target in wasm32-wasip1 wasm32-wasip1-threads; do
    [ -s "$directory/share/wasi-sysroot/include/$target/stdio.h" ] || return 1
    [ -s "$directory/share/wasi-sysroot/include/$target/eh/c++/v1/format" ] || return 1
    [ -s "$directory/share/wasi-sysroot/lib/$target/libc.a" ] || return 1
    [ -s "$directory/share/wasi-sysroot/lib/$target/eh/libc++.a" ] || return 1
  done
}

if sdk_complete "$dest"; then
  echo "already installed: $dest ($VERSION)"
  exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
echo "fetching $URL"
curl -fL --retry 3 -o "$work/$TARBALL" "$URL"
echo "$SHA256  $work/$TARBALL" | sha256sum -c -
mkdir -p "$dest"
tar -xzf "$work/$TARBALL" -C "$dest" --strip-components=1
sdk_complete "$dest" || { echo "incomplete wasi-sdk installation: $dest" >&2; exit 1; }
echo "installed: $dest ($VERSION)"
