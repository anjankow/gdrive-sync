#!/usr/bin/env bash
# Downloads the Debian package of gdrive-sync and installs it with apt, which
# also installs the libraries it needs (Ubuntu 24.04, amd64).
#
#   scripts/install-deb.sh [latest | <version> | build]
#
#   latest     the newest GitHub release (default)
#   <version>  a release, e.g. 0.1.0
#   build      the package CI built for the latest merge to main
#              (needs the GitHub CLI, signed in: gh auth login)
#
# Installing an older or newer version replaces the installed one.
set -euo pipefail
# shellcheck source=lib/download.sh
. "$(dirname "${BASH_SOURCE[0]}")/lib/download.sh"

case "${1:-latest}" in
    -h | --help)
        sed -n '2,/^[^#]/p' "$0" | sed '$d; s/^# \{0,1\}//'
        exit 0
        ;;
esac
source="${1:-latest}"

[ "$(dpkg --print-architecture 2>/dev/null)" = "amd64" ] || die "the package is for amd64 Debian/Ubuntu systems"
command -v apt >/dev/null || die "apt is required"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
chmod 755 "$tmp" # apt reads the file as the user _apt
deb="$(download_package deb "$source" "$tmp")"
chmod 644 "$deb"

echo "Installing $(basename "$deb") ..."
sudo apt install -y "$deb"
echo "Installed. Start it from the application menu (Google Drive Sync) or with: gdrive-sync"
