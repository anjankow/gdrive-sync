[![CI](https://github.com/anjankow/gdrive-sync/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/anjankow/gdrive-sync/actions/workflows/ci.yml)

# gdrive-sync

![Google Drive](https://img.shields.io/badge/Google%20Drive-%234285F4.svg?style=for-the-badge&logo=googledrive&logoColor=white)

Google Drive sync for Linux (KDE). Syncs selected Google Drive folders with local directories.

<img width="897" height="709" alt="image" src="https://github.com/user-attachments/assets/cf4d3d3c-5a66-4a1a-8c75-30e109a68969" />


- C++17, Qt 5 Widgets and KDE Frameworks 5 (targets Ubuntu 24.04 / Plasma 5.27)
- Sign-in through the web browser (OAuth 2.0 with PKCE); the sign-in data is kept in the KDE wallet
- Several Drive folders can be mapped simultaneously
- Sync on request and automatic
- Every deletion must be confirmed before anything is removed


> [!NOTE] 
> 100% vibe-coded by Claude Code and rigorously tested by @anjankow. 

- Confirmed working ✔️
- Confirmed no data lost ✔️
- Confirmed no shady work was done behind the scenes ✔️
- Confirmed no personal data is collected nor abused ✔️


## Build

```bash
sudo apt install build-essential cmake ninja-build extra-cmake-modules gettext qtbase5-dev \
  libkf5wallet-dev libkf5config-dev libkf5coreaddons-dev libkf5i18n-dev libkf5notifications-dev \
  libkf5kio-dev libkf5widgetsaddons-dev libkf5dbusaddons-dev libkf5windowsystem-dev
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

1. Go to https://console.cloud.google.com/ and create a new project (no credit card required).
2. Enable the **Google Drive API**.
3. Search for Google Drive API or go to https://console.cloud.google.com/marketplace/product/google/drive.googleapis.com.
   Enable it.
4. Create credentials → OAuth Client ID → Desktop App → select scope `https://www.googleapis.com/auth/drive`
5. Add yourself as a test user: **Google Auth platform → Audience → Test users → Add users**, enter the Google
   account you will sign in with.
6. Start `gdrive-sync`, open **Settings → Google API Client** and enter the client ID and secret. Extract them from the secret JSON file received from gcloud.


## Usage

1. **Sign In** with your google account via the browser. Choose the sync direction (two-way, Drive to local,
   or local to Drive). It applies to all folders until you sign out.
2. **Add Folder**: pick a Drive folder and a local destination. The destination may already hold files:
   the first sync uploads them to Drive (two-way and local to Drive; with Drive to local they stay local only).
   A file that exists on both sides with different content is asked about, in every direction.
3. **Sync Selected** / **Sync All**.
4. **Settings → Automatic Sync**: tick the box and choose the interval. This installs and starts
   `gdrive-sync.timer` in `~/.config/systemd/user`; unticking removes it again.


## Behavior

- Files deleted by a sync are moved to the trash (local trash and Drive trash), never destroyed directly.
- Automatic runs never delete anything and never resolve conflicts. They show a notification, and the next
  manual sync asks you.
- Google Docs, Sheets and Slides have no file content and are skipped.
- **Unmount** deletes the local copy of that folder. For a folder that already held files when it was
  added, you choose to keep all its local files or delete all of them. Google Drive is not touched.
- **Sign Out** revokes the sign-in at Google and forgets the folder list and the timer (if automatic sync is enabled). Local files stay.


# Internals

## Layout

- `src/core`: application logic without widgets (OAuth, Drive client, planner, sync engine, folders, timer)
- `src/kde`: KWallet, Places and notification back ends
- `src/gui`: main window and dialogs
- `tests`: test suites and the mock Google server
- `scripts`: version bump helper and the script that runs the latest CI build


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
| Destination (empty or not); several independent folders; unmount | `tst_folders`, `tst_sync`, `tst_gui` |
| Sync on request; directions; conflicts | `tst_sync`, `tst_planner` |
| Automatic sync (systemd timer, unattended run) | `tst_autosync`, `tst_gui` |
| Deletion warning, cancellable | `tst_deletions`, `tst_gui`, `tst_planner` |
| Progress bar and synced/remaining bytes | `tst_progress`, `tst_gui` |
| Folder appears in Places | `tst_places` |
| Window always responsive | `tst_responsiveness` |
| Log file with severities, configurable location | `tst_logging`, `tst_gui` |
| Drive REST client (paging, retries, resumable upload) | `tst_driveclient` |

### Live tests against Google Drive

Google offers no sandbox or test account for the Drive API. `tst_live_drive` therefore only runs when you
provide credentials of a Google account dedicated to testing; it is skipped otherwise (always in CI):

```bash
GDRIVESYNC_LIVE_CLIENT_ID=... GDRIVESYNC_LIVE_CLIENT_SECRET=... GDRIVESYNC_LIVE_REFRESH_TOKEN=... build/bin/tst_live_drive
```
