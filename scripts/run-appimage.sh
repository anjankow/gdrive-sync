#!/usr/bin/env bash
# Downloads the AppImage of gdrive-sync into the current directory and starts
# it. Nothing is installed.
#
#   scripts/run-appimage.sh [latest | <version>] [-- <gdrive-sync arguments>]
#
#   latest     the newest GitHub release (default)
#   <version>  a release, e.g. 0.1.0
#
# Later you can start the downloaded file directly: ./gdrive-sync-<version>.AppImage
set -euo pipefail
# shellcheck source=lib/download.sh
. "$(dirname "${BASH_SOURCE[0]}")/lib/download.sh"

source="latest"
case "${1:-}" in
    -h | --help)
        sed -n '2,/^[^#]/p' "$0" | sed '$d; s/^# \{0,1\}//'
        exit 0
        ;;
    "" | --) ;;
    *)
        source="$1"
        shift
        ;;
esac
if [ "${1:-}" = "--" ]; then
    shift
fi

[ "$(uname -m)" = "x86_64" ] || die "the AppImage is for x86_64 machines"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
downloaded="$(download_package appimage "$source" "$tmp")"
appimage="$PWD/$(basename "$downloaded")"
mv -f "$downloaded" "$appimage"
chmod +x "$appimage"
echo "Saved $appimage"

# AppImages are mounted with FUSE 2; without it they unpack themselves instead.
if ldconfig -p 2>/dev/null | grep -q 'libfuse\.so\.2'; then
    exec "$appimage" "$@"
fi
echo "libfuse2 is not installed: starting without FUSE (sudo apt install libfuse2t64 avoids this)."
exec "$appimage" --appimage-extract-and-run "$@"
