#!/usr/bin/env bash
# Deploy Qt into the .app, make it self-contained, ad-hoc sign it, and build a DMG.
#
#   bash packaging/macos/bundle-mac.sh [build-dir] [output-dir]
#
# Needs macdeployqt for the Qt being bundled on PATH.
set -euo pipefail

BUILD_DIR="$(cd "${1:-build}" && pwd)"
OUT_DIR="${2:-dist}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
SRC_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(sed -n 's/^project(anyprocessorqt VERSION \([0-9.]*\).*/\1/p' "$SRC_DIR/CMakeLists.txt")"
ARCH="$(uname -m)"

STAGE="$BUILD_DIR/macstage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$BUILD_DIR/anyprocessorqt.app" "$STAGE/"
APP="$STAGE/anyprocessorqt.app"
BIN="$APP/Contents/MacOS/anyprocessorqt"

macdeployqt "$APP" -always-overwrite

# Absolute rpaths point at the build machine's Qt; drop them so the bundle can't
# silently depend on it. Frameworks are found through @executable_path.
otool -l "$BIN" | awk '/cmd LC_RPATH/{getline; getline; print $2}' | while read -r rp; do
    case "$rp" in
        @*) ;;
        *) echo "removing rpath $rp"; install_name_tool -delete_rpath "$rp" "$BIN" ;;
    esac
done
if ! otool -l "$BIN" | grep -q "@executable_path/../Frameworks"; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN"
fi

# Binaries were modified after linking: re-sign (ad-hoc) or arm64 refuses to run them.
codesign --force --deep --sign - "$APP"

# ---- verify
fail=0
[ -d "$APP/Contents/Frameworks/QtCore.framework" ] || { echo "MISSING QtCore.framework" >&2; fail=1; }
[ -f "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ] || { echo "MISSING libqcocoa.dylib" >&2; fail=1; }
# Qt references must be @rpath/@executable_path; @rpath is fine because the
# framework is inside Contents/Frameworks (macdeployqt in Qt 6.10 leaves @rpath).
# (skip line 1: it is the binary's own path, which contains "qt")
if otool -L "$BIN" | tail -n +2 | grep -E 'Qt[A-Za-z0-9]*\.framework' | grep -vE '@rpath|@executable_path|@loader_path' ; then
    echo "absolute Qt references remain" >&2
    fail=1
fi
[ "$fail" -eq 0 ] || exit 1

DMG="$OUT_DIR/anyprocessorqt-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname anyprocessorqt -srcfolder "$STAGE" -ov -format UDZO "$DMG"
ls -la "$DMG"
