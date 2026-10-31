#!/usr/bin/env bash
# Build an AppImage from a CMake build tree.
#
#   bash packaging/linux/bundle-appimage.sh [build-dir] [output-dir]
#
# Needs qmake (or qmake6) for the Qt being bundled on PATH, and network access
# to download linuxdeploy on first use (cached in <build-dir>/tools).
#
# Checks, and fails the build if missing:
#   - Wayland shell integration plugins (xdg-shell). Without them Qt prints
#     'No shell integration named "xdg-shell" found' and cannot open a window
#     under Wayland, e.g. the ChromeOS Linux container.
#   - libxcb-cursor, which the xcb plugin needs since Qt 6.5 and which a fresh
#     Debian/ChromeOS container does not have.
#   - The offscreen platform plugin, used by the --roundtrip/--pdf/... CLI modes.
set -euo pipefail

BUILD_DIR="$(cd "${1:-build}" && pwd)"
OUT_DIR="${2:-dist}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
SRC_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ARCH="$(uname -m)"
VERSION="$(sed -n 's/^project(anyprocessorqt VERSION \([0-9.]*\).*/\1/p' "$SRC_DIR/CMakeLists.txt")"
APPDIR="$BUILD_DIR/AppDir"
TOOLS="$BUILD_DIR/tools"

QMAKE_BIN="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
if [ -z "$QMAKE_BIN" ]; then
    echo "qmake not found; put the Qt to bundle on PATH or set QMAKE" >&2
    exit 1
fi
export QMAKE="$QMAKE_BIN"
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
QT_LIBS="$("$QMAKE" -query QT_INSTALL_LIBS)"
echo "Qt $("$QMAKE" -query QT_VERSION) plugins: $QT_PLUGINS"

mkdir -p "$TOOLS"
fetch() {
    local name="$1" url="$2"
    if [ ! -x "$TOOLS/$name" ]; then
        curl -fsSL -o "$TOOLS/$name" "$url"
        chmod +x "$TOOLS/$name"
    fi
}
fetch "linuxdeploy-$ARCH.AppImage" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
fetch "linuxdeploy-plugin-qt-$ARCH.AppImage" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage"
export PATH="$TOOLS:$PATH"
# CI runners and containers have no FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
# Let linuxdeploy resolve libraries from the bundled Qt first.
export LD_LIBRARY_PATH="$QT_LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# Platform plugins beyond xcb: Wayland (ChromeOS, modern desktops) and offscreen (CLI modes).
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export EXTRA_QT_MODULES="waylandclient"

"$TOOLS/linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" --desktop-file "$APPDIR/usr/share/applications/anyprocessorqt.desktop" --icon-file "$SRC_DIR/packaging/icons/anyprocessorqt-256.png" --icon-filename anyprocessorqt --plugin qt

# Wayland client plugin families: copy any the Qt plugin didn't deploy.
DEPLOY_DIRS=()
for family in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    if [ -d "$QT_PLUGINS/$family" ] && [ ! -d "$APPDIR/usr/plugins/$family" ]; then
        echo "adding $family"
        mkdir -p "$APPDIR/usr/plugins/$family"
        cp -a "$QT_PLUGINS/$family/." "$APPDIR/usr/plugins/$family/"
        DEPLOY_DIRS+=(--deploy-deps-only "$APPDIR/usr/plugins/$family")
    fi
done

# Qt >= 6.5 links the xcb plugin against libxcb-cursor. Fresh Debian/ChromeOS
# containers don't have it, so make sure it ships even if linuxdeploy skipped it.
NEED_XCB_CURSOR=0
for f in "$APPDIR"/usr/lib/libQt6XcbQpa.so.6 "$APPDIR"/usr/plugins/platforms/libqxcb.so; do
    if [ -e "$f" ] && ldd "$f" 2>/dev/null | grep -q 'libxcb-cursor'; then
        NEED_XCB_CURSOR=1
    fi
done
if [ "$NEED_XCB_CURSOR" -eq 1 ] && ! compgen -G "$APPDIR/usr/lib/libxcb-cursor.so.0*" >/dev/null; then
    src="$(ldconfig -p | awk '/libxcb-cursor\.so\.0 /{print $NF; exit}')"
    if [ -z "$src" ]; then
        echo "libxcb-cursor.so.0 not installed on the build host (apt install libxcb-cursor0)" >&2
        exit 1
    fi
    echo "adding $src"
    cp -L "$src" "$APPDIR/usr/lib/libxcb-cursor.so.0"
fi

# Second pass: pull in dependencies of anything added above, then write the AppImage.
OUTPUT_NAME="anyprocessorqt-$VERSION-$ARCH.AppImage"
export LDAI_OUTPUT="$OUT_DIR/$OUTPUT_NAME"
export OUTPUT="$OUT_DIR/$OUTPUT_NAME"
"$TOOLS/linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" "${DEPLOY_DIRS[@]}" --output appimage

# ---- verify
fail=0
need() {
    if compgen -G "$APPDIR/$1" >/dev/null; then
        echo "ok      $1"
    else
        echo "MISSING $1" >&2
        fail=1
    fi
}
need "usr/plugins/platforms/libqxcb.so"
need "usr/plugins/platforms/libqwayland-generic.so"
need "usr/plugins/platforms/libqoffscreen.so"
need "usr/plugins/wayland-shell-integration/libxdg-shell.so"
if [ "$NEED_XCB_CURSOR" -eq 1 ]; then
    need "usr/lib/libxcb-cursor.so.0*"
fi
need "usr/lib/libQt6WaylandClient.so.6*"
if [ "$fail" -ne 0 ]; then
    echo "AppImage is incomplete" >&2
    exit 1
fi
ls -la "$OUT_DIR/$OUTPUT_NAME"
