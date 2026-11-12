#!/usr/bin/env bash
# Builds gdrive-sync-<version>-x86_64.AppImage from a configured and built
# CMake build directory. The AppImage carries Qt 5 and the KDE Frameworks
# libraries the application uses, so it runs without installing anything.
#
#   scripts/build-appimage.sh <build-dir> <version>
#
# Tools: linuxdeploy and its Qt plugin, downloaded into <build-dir>/appimage-tools
# (or $GDS_APPIMAGE_TOOLS) unless they are already there.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <build-dir> <version>" >&2
    exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(cd "$1" && pwd)"
version="$2"
tools="${GDS_APPIMAGE_TOOLS:-$build/appimage-tools}"
appdir="$build/AppDir"
output="gdrive-sync-${version}-x86_64.AppImage"

mkdir -p "$tools"
fetch() {
    if [ ! -x "$tools/$1" ]; then
        curl -fsSL --retry 3 -o "$tools/$1" "$2"
        chmod +x "$tools/$1"
    fi
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

rm -rf "$appdir" "$build/$output"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr
mkdir -p "$appdir/apprun-hooks"
cp "$repo/packaging/appimage/apprun-hook.sh" "$appdir/apprun-hooks/gdrive-sync.sh"

# The tools are AppImages themselves; CI machines have no FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH" # linuxdeploy finds its plugins in PATH
export QMAKE="${QMAKE:-/usr/lib/qt5/bin/qmake}"
export LDAI_OUTPUT="$output"
export LINUXDEPLOY_OUTPUT_VERSION="$version"

cd "$build"
linuxdeploy-x86_64.AppImage \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/gdrive-sync" \
    --desktop-file "$appdir/usr/share/applications/io.github.anjankow.gdrivesync.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/io.github.anjankow.gdrivesync.png" \
    --plugin qt \
    --output appimage

test -f "$build/$output"
echo "Built $build/$output"

# Smoke test: the bundle holds the KDE libraries and data, and the binary starts.
check="$build/appimage-check"
rm -rf "$check" && mkdir -p "$check"
(cd "$check" && "$build/$output" --appimage-extract >/dev/null)
for f in usr/bin/gdrive-sync usr/share/knotifications5/gdrive-sync.notifyrc apprun-hooks/gdrive-sync.sh; do
    test -e "$check/squashfs-root/$f" || { echo "missing in the AppImage: $f" >&2; exit 1; }
done
for lib in libKF5Wallet libKF5Notifications libKF5KIOFileWidgets libQt5Widgets; do
    ls "$check/squashfs-root/usr/lib/$lib".so* >/dev/null 2>&1 || { echo "not bundled: $lib" >&2; exit 1; }
done
QT_QPA_PLATFORM=offscreen "$build/$output" --appimage-extract-and-run --version
rm -rf "$check"
