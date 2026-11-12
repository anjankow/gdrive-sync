#!/usr/bin/env bash
# Tests scripts/changelog.sh, which the release workflows use.
#   tests/test_changelog.sh <path to changelog.sh>
set -euo pipefail
script="$1"
dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
file="$dir/CHANGELOG.md"
fail() {
    echo "FAIL: $*" >&2
    exit 1
}

cat > "$file" <<'MD'
# Changelog

## Unreleased

### Sync
- New thing.

## 0.2.0 – 2026-12-01

- Second release.

## 0.1.0 – first release

First release text.
MD

[ "$("$script" notes Unreleased "$file")" = "$(printf '### Sync\n- New thing.')" ] || fail "Unreleased notes"
[ "$("$script" notes 0.2.0 "$file")" = "- Second release." ] || fail "notes of a dated section"
[ "$("$script" notes 0.1.0 "$file")" = "First release text." ] || fail "notes of the last section"
! "$script" notes 9.9.9 "$file" 2>/dev/null || fail "a missing section must fail"

before="$(grep -v '^## Unreleased' "$file")"
"$script" release 0.3.0 2027-01-15 "$file"
grep -q '^## 0.3.0 – 2027-01-15$' "$file" || fail "the Unreleased section was not renamed"
! grep -q '^## Unreleased' "$file" || fail "Unreleased heading left over"
[ "$(grep -v '^## 0.3.0' "$file")" = "$before" ] || fail "other lines changed"
[ "$("$script" notes 0.3.0 "$file")" = "$(printf '### Sync\n- New thing.')" ] || fail "notes after the release"
! "$script" release 0.4.0 2027-02-01 "$file" 2>/dev/null || fail "a release without Unreleased must fail"

printf '# Changelog\n\n## Unreleased\n\n## 0.1.0 – x\n\n- a\n' > "$file"
! "$script" notes Unreleased "$file" 2>/dev/null || fail "an empty Unreleased section must fail"
echo "changelog.sh: all checks passed"
