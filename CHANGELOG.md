# Changelog

## 0.1.0 – first release

A KDE application for Ubuntu 24.04 (Plasma 5, Qt 5.15, KDE Frameworks 5) that syncs selected
Google Drive folders with local folders.

### Account
- Sign in with a Google account in the web browser (OAuth 2.0 with PKCE and a local redirect).
- Secrets (client ID and secret, refresh token) are kept only in the KDE wallet.

### Folders
- Map any number of Drive folders to local folders. Each one is synced and unmounted on its own.
- Map the whole of My Drive to one local folder. It cannot be combined with mappings of its
  folders, so no file is synced twice.
- The local folder may be new, empty or already hold files. Files already there are uploaded on
  the first sync, unless the direction is Drive to local.

### Sync
- Sync direction chosen after sign-in: two-way, Drive to local, or local to Drive. It applies to
  all folders until sign-out.
- Sync on request (selected folder or all), cancellable at any time.
- Progress bar with bytes synced and remaining, and the file being transferred.
- Every deletion is listed for confirmation before anything is deleted. Deleted files go to the
  local trash or the Drive trash.
- Conflicts are asked about per file: keep both, local version, Drive version, or decide later.
  Files with the same path but different content before the first sync are asked about in every
  direction.
- Google Docs, Sheets and Slides are skipped.
- Resumable uploads, retries with backoff on rate limits and server errors, and an automatic
  access-token refresh.

### Automatic sync
- Optional systemd user timer with a chosen interval, installed only when enabled and removed
  when disabled.
- Automatic runs sync all folders, never delete and never resolve conflicts. They notify you of
  what was skipped and of failures; the next manual sync asks.

### Application
- The window stays responsive during syncs, the wallet password prompt, the sign-in and other
  slow operations.
- Log file `gdrive-sync.log` (default `/tmp/gdrive-sync-<user>`, configurable under
  Settings → Log Location).

### Distribution
- `.deb` package for Ubuntu 24.04 amd64, built by CI on every merge to main.
  `scripts/run-latest-build.sh` downloads the latest build and runs it without installing it.
- Manual release workflow (patch, minor or major) that publishes the `.deb` as a GitHub release.
