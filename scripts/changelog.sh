#!/usr/bin/env bash
# Reads and updates CHANGELOG.md for releases.
#
#   scripts/changelog.sh notes <section> [file]
#       Prints the text of a section: "Unreleased" or a version ("0.1.0").
#       Fails when the section is missing or empty.
#   scripts/changelog.sh release <version> <date> [file]
#       Renames the "Unreleased" section to "<version> – <date>".
#
# Sections are level-2 headings: "## Unreleased", "## 0.1.0 – <text>".
set -euo pipefail

die() {
    echo "error: $*" >&2
    exit 1
}

command="${1:-}"
case "$command" in
    notes)
        section="${2:-}"
        file="${3:-CHANGELOG.md}"
        [ -n "$section" ] || die "usage: $0 notes <section> [file]"
        [ -f "$file" ] || die "$file not found"
        text="$(awk -v want="$section" '
            /^## / {
                if (inside) exit
                heading = substr($0, 4)
                split(heading, words, " ")
                inside = (heading == want || words[1] == want)
                next
            }
            inside { print }
        ' "$file")"
        # drop leading and trailing blank lines
        text="$(printf '%s\n' "$text" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')"
        [ -n "$text" ] || die "the section \"$section\" of $file is missing or empty"
        printf '%s\n' "$text"
        ;;
    release)
        version="${2:-}"
        date="${3:-}"
        file="${4:-CHANGELOG.md}"
        [ -n "$version" ] && [ -n "$date" ] || die "usage: $0 release <version> <date> [file]"
        grep -q '^## Unreleased[[:space:]]*$' "$file" || die "$file has no \"## Unreleased\" section"
        sed -i "s/^## Unreleased[[:space:]]*\$/## ${version} – ${date}/" "$file"
        ;;
    *)
        sed -n '2,/^[^#]/p' "$0" | sed '$d; s/^# \{0,1\}//'
        [ "$command" = "-h" ] || [ "$command" = "--help" ] || exit 2
        ;;
esac
