# gdrive-sync

Google Drive sync for Linux (KDE). Syncs selected Google Drive folders with local directories.

- C++17, Qt 5 Widgets and KDE Frameworks 5 (targets Ubuntu 24.04 / Plasma 5.27)
- Sign-in through the web browser (OAuth 2.0 with PKCE); the sign-in data is kept in the KDE wallet
- Several Drive folders, each mapped to its own empty local directory and shown in Dolphin's Places
- Sync on request, with a progress bar and synced/remaining bytes
- Every deletion is listed and must be confirmed before anything is removed
- Optional automatic sync through a systemd user timer
- The window stays responsive: network, file and process work runs in background threads

## Build

```bash
sudo apt install build-essential cmake ninja-build extra-cmake-modules gettext qtbase5-dev \
  libkf5wallet-dev libkf5config-dev libkf5coreaddons-dev libkf5i18n-dev libkf5notifications-dev \
  libkf5kio-dev libkf5widgetsaddons-dev libkf5dbusaddons-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
sudo cmake --install build
```

## Google Cloud setup (once)

Google has no shared client for third-party sync tools, so the application uses an OAuth client of your own
Google Cloud project:

1. Create a project in the Google Cloud console and enable the **Google Drive API**.
2. Configure the OAuth consent screen and add your Google account as a test user. While the project is in
   "Testing" status Google expires the sign-in after 7 days; publish the app ("In production") to avoid that.
3. Create an OAuth client ID of type **Desktop app**.
4. Start `gdrive-sync`, press **Google API Client…** and enter the client ID and secret.

The application requests the `https://www.googleapis.com/auth/drive` scope, which is needed to sync folders
that already exist in your Drive.

## Usage

1. **Sign In** opens the browser. After the sign-in choose the sync direction (two-way, Drive to local, or
   local to Drive). It applies to all folders until you sign out.
2. **Add Folder…**: pick a Drive folder and an empty local destination.
3. **Sync Selected** / **Sync All**.
4. **Automatic Sync**: tick the box, choose the interval and press **Apply**. This installs and starts
   `gdrive-sync.timer` in `~/.config/systemd/user`; unticking removes it again.

Behaviour worth knowing:

- Files deleted by a sync are moved to the trash (local trash and Drive trash), never destroyed directly.
- Automatic runs never delete anything and never resolve conflicts. They show a notification, and the next
  manual sync asks you.
- Google Docs, Sheets and Slides have no file content and are skipped.
- **Unmount** deletes the local copy of that folder. Google Drive is not touched.
- **Sign Out** revokes the sign-in at Google and forgets the folder list and the timer. Local files stay.

`gdrive-sync --sync` runs one unattended sync of all folders and exits (this is what the timer starts).

## Tests

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
```

```bash
cmake --build build
```

```bash
xvfb-run -a dbus-run-session -- ctest --test-dir build --output-on-failure
```

The tests run against an in-process mock of the Google OAuth and Drive v3 endpoints (`tests/mock`).

| Requirement | Tests |
|---|---|
| Sign in, sign out, sign in with another account; secrets only in the wallet | `tst_account`, `tst_gui` |
| Select a Drive folder (not the whole drive) | `tst_sync`, `tst_folders`, `tst_gui` |
| Destination must be empty; several independent folders; unmount | `tst_folders`, `tst_gui` |
| Sync on request; directions; conflicts | `tst_sync`, `tst_planner` |
| Automatic sync (systemd timer, unattended run) | `tst_autosync`, `tst_gui` |
| Deletion warning, cancellable | `tst_deletions`, `tst_gui`, `tst_planner` |
| Progress bar and synced/remaining bytes | `tst_progress`, `tst_gui` |
| Folder appears in Dolphin (Places) | `tst_places` |
| Window always responsive | `tst_responsiveness` |
| Drive REST client (paging, retries, resumable upload) | `tst_driveclient` |

### Live tests against Google Drive

Google offers no sandbox or test account for the Drive API. `tst_live_drive` therefore only runs when you
provide credentials of a Google account dedicated to testing; it is skipped otherwise (always in CI):

```bash
GDRIVESYNC_LIVE_CLIENT_ID=... GDRIVESYNC_LIVE_CLIENT_SECRET=... GDRIVESYNC_LIVE_REFRESH_TOKEN=... build/bin/tst_live_drive
```

## Packages and releases

- **Every merge to `main`**: CI builds `gdrive-sync_<version>+git<date>.<sha>_amd64.deb` for Ubuntu 24.04
  (amd64, Qt 5.15, KF 5.115) and attaches it to the CI run as the artifact `gdrive-sync-deb`.
- **Try the latest build without installing it** (needs the GitHub CLI, signed in with `gh auth login`):

  ```bash
  scripts/run-latest-build.sh
  ```

  The package is downloaded and unpacked into `~/.cache/gdrive-sync-test` and started from there. It uses
  your normal configuration and wallet. Do not enable the automatic sync from such a copy: the timer would
  point into the cache directory.
- **Release**: on GitHub open *Actions → Release → Run workflow* on `main` and choose `patch`, `minor` or
  `major`. The workflow increases the latest `vX.Y.Z` tag accordingly (the first release is `0.1.0`), runs
  the tests, tags the commit and publishes a GitHub release with the `.deb`.

## Layout

- `src/core`: application logic without widgets (OAuth, Drive client, planner, sync engine, folders, timer)
- `src/kde`: KWallet, Places and notification back ends
- `src/gui`: main window and dialogs
- `tests`: test suites and the mock Google server
- `scripts`: version bump helper and the script that runs the latest CI build
