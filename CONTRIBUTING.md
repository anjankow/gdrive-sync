# Contributing

## Build from source

Install the build dependencies (Ubuntu 24.04):

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
| Select a Drive folder or the whole drive | `tst_sync`, `tst_folders`, `tst_gui` |
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
