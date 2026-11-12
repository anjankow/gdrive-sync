---
name: changelog
description: Format of CHANGELOG.md and how to keep its "Unreleased" section current. Use whenever a change in this repository is user visible (a feature, a changed behavior, a fix users notice, a change to installing or running the app), and whenever such a change is modified or reverted before the next release.
---

# Changelog

`CHANGELOG.md` is the source of the release notes: the Release workflow publishes the
`## Unreleased` section as the notes of the new release and then renames that heading to
`## <version> – <date>`. Keep it accurate, because it is what users read.

## Format

```markdown
# Changelog

## Unreleased

### <Area>
- <What users get, in one or two sentences.>

## 0.2.0 – 2027-01-15
...

## 0.1.0 – first release
...
```

- Sections are level-2 headings, newest first, directly below `# Changelog`.
- `## Unreleased` is the only section that is ever edited. It exists only while there are
  unreleased changes; create it above the newest release section when the first one comes.
- Released sections (`## <version> – ...`) are never edited, not even for typos. Do not add
  version sections by hand; the Release workflow names them.
- Inside a section, group entries under `### <Area>` headings. Reuse the areas of the existing
  entries (Account, Folders, Sync, Automatic sync, Application, Distribution) and add a new one
  only when nothing fits.
- Match the style of the existing entries: short bullet points starting with `- `, describing
  what users can do or what they see, in plain words, wrapped at about 100 characters. No PR
  numbers, commit hashes, class or function names, or file names of the source code.

## Keeping "Unreleased" current

- Add an entry in the same change (commit or pull request) that makes the user-visible change.
- The section is a summary of the current state, not a history. When a requirement changes
  before the release, rewrite the existing entry to describe the final behavior. When a change
  is reverted, remove its entry. Merge entries about the same feature into one.
- A fix of something that was never released is not an entry of its own: adjust the entry of
  the feature instead.

## What belongs in it

Include changes users notice:
- new features and changed behavior of the application
- fixes of problems users could run into in a released version
- changes to installing, running or updating the application (packages, scripts users run,
  system requirements)

Leave out:
- CI, build and release automation, tests, refactoring, code style, dependency bumps without a
  visible effect
- changes to README or other documentation only
