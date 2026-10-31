#!/usr/bin/env bash
# Deploy Qt next to the exe and zip it (run from Git Bash / GitHub Actions bash on Windows).
#
#   bash packaging/windows/bundle-windows.sh [build-dir] [output-dir]
#
# Needs windeployqt for the Qt being bundled on PATH and an MSVC environment
# (for --compiler-runtime).
set -euo pipefail

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-dist}"
mkdir -p "$OUT_DIR"
SRC_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -n 's/^project(anyprocessorqt VERSION \([0-9.]*\).*/\1/p' "$SRC_DIR/CMakeLists.txt")"
NAME="anyprocessorqt-$VERSION-windows-x64"
PKG="$OUT_DIR/$NAME"

EXE="$BUILD_DIR/Release/anyprocessorqt.exe"
[ -f "$EXE" ] || EXE="$BUILD_DIR/anyprocessorqt.exe"
[ -f "$EXE" ] || { echo "anyprocessorqt.exe not found in $BUILD_DIR" >&2; exit 1; }

rm -rf "$PKG"
mkdir -p "$PKG"
cp "$EXE" "$PKG/"
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "$PKG/anyprocessorqt.exe"
cp "$SRC_DIR/LICENSE" "$SRC_DIR/README.md" "$PKG/"

# ---- verify
fail=0
for f in Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6PrintSupport.dll platforms/qwindows.dll; do
    if [ -f "$PKG/$f" ]; then echo "ok      $f"; else echo "MISSING $f" >&2; fail=1; fi
done
[ "$fail" -eq 0 ] || exit 1

rm -f "$OUT_DIR/$NAME.zip"
(cd "$OUT_DIR" && 7z a -tzip "$NAME.zip" "$NAME" >/dev/null)
ls -la "$OUT_DIR/$NAME.zip"
