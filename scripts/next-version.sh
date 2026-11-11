#!/usr/bin/env bash
# Prints the next release version.
#
#   scripts/next-version.sh <patch|minor|major> [current-version]
#
# Without a current version the latest "vX.Y.Z" git tag is used; when there
# is no such tag yet, the first release is 0.1.0 regardless of the bump.
set -euo pipefail

bump="${1:-}"
current="${2:-}"

case "$bump" in
    patch | minor | major) ;;
    *)
        echo "usage: $0 <patch|minor|major> [current-version]" >&2
        exit 2
        ;;
esac

if [ -z "$current" ]; then
    current="$(git tag --list 'v[0-9]*.[0-9]*.[0-9]*' --sort=-v:refname | head -n 1)"
    if [ -z "$current" ]; then
        echo "0.1.0"
        exit 0
    fi
fi
current="${current#v}"

if ! [[ "$current" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "not a MAJOR.MINOR.PATCH version: $current" >&2
    exit 2
fi
major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"

case "$bump" in
    major) echo "$((major + 1)).0.0" ;;
    minor) echo "${major}.$((minor + 1)).0" ;;
    patch) echo "${major}.${minor}.$((patch + 1))" ;;
esac
