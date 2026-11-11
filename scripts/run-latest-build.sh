#!/usr/bin/env bash
# Downloads the .deb built by CI for the latest merge to main and runs the
# application from it WITHOUT installing the package.
#
#   scripts/run-latest-build.sh [--download-only] [-- <gdrive-sync arguments>]
#
# Requirements: the GitHub CLI, signed in (gh auth login), and the runtime
# libraries of the package (the script tells you which are missing).
#
# The package is unpacked into ~/.cache/gdrive-sync-test and started from
# there. Your normal configuration, wallet entries and synced folders are
# used, exactly as an installed copy would.
set -euo pipefail

REPO="${GDRIVE_SYNC_REPO:-anjankow/gdrive-sync}"
WORKFLOW="ci.yml"
ARTIFACT="gdrive-sync-deb"
BRANCH="main"
DEST="${GDRIVE_SYNC_TEST_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/gdrive-sync-test}"

download_only=0
if [ "${1:-}" = "--download-only" ]; then
    download_only=1
    shift
fi
if [ "${1:-}" = "--" ]; then
    shift
fi

die() {
    echo "error: $*" >&2
    exit 1
}

command -v gh >/dev/null || die "the GitHub CLI is required: sudo apt install gh && gh auth login"
command -v dpkg-deb >/dev/null || die "dpkg-deb is required (package dpkg)"
gh auth status >/dev/null 2>&1 || die "not signed in to GitHub: run 'gh auth login'"

echo "Looking for the latest successful build of $BRANCH in $REPO ..."
run_id="$(gh run list --repo "$REPO" --workflow "$WORKFLOW" --branch "$BRANCH" --event push \
    --status success --limit 1 --json databaseId --jq '.[0].databaseId // empty')"
[ -n "$run_id" ] || die "no successful CI run on $BRANCH found"

download="$DEST/download"
root="$DEST/root"
rm -rf "$download" "$root"
mkdir -p "$download" "$root"

gh run download "$run_id" --repo "$REPO" --name "$ARTIFACT" --dir "$download" \
    || die "run $run_id has no artifact named $ARTIFACT (artifacts expire after 90 days)"
deb="$(find "$download" -name '*.deb' | head -n 1)"
[ -n "$deb" ] || die "the artifact contains no .deb file"

echo "Downloaded $(basename "$deb") (CI run $run_id)"
dpkg-deb -x "$deb" "$root"
binary="$root/usr/bin/gdrive-sync"
[ -x "$binary" ] || die "the package does not contain usr/bin/gdrive-sync"

# Report runtime libraries that are not installed on this machine.
missing="$(ldd "$binary" | awk '/not found/ {print $1}')"
if [ -n "$missing" ]; then
    echo "These libraries are missing on this system:" >&2
    echo "$missing" | sed 's/^/  /' >&2
    echo "The package depends on:" >&2
    dpkg-deb -f "$deb" Depends | tr ',' '\n' | sed 's/^ */  /' >&2
    die "install the missing libraries, or install the package itself: sudo apt install '$deb'"
fi

if [ "$download_only" -eq 1 ]; then
    echo "Package: $deb"
    echo "Unpacked to: $root"
    exit 0
fi

# Let KDE find the notification configuration and desktop file of the unpacked copy.
export XDG_DATA_DIRS="$root/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
echo "Starting $binary"
exec "$binary" "$@"
