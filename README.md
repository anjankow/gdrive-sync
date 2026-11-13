[![CI](https://github.com/anjankow/gdrive-sync/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/anjankow/gdrive-sync/actions/workflows/ci.yml)
[![Release](https://img.shields.io/badge/release-v0.1.0-blue)](https://github.com/anjankow/gdrive-sync/releases/latest)

# gdrive-sync

![Google Drive](https://img.shields.io/badge/Google%20Drive-%234285F4.svg?style=for-the-badge&logo=googledrive&logoColor=white)

Google Drive sync for Linux (KDE). Syncs selected Google Drive folders with local directories.

<img width="909" height="745" alt="image" src="https://github.com/user-attachments/assets/60f3ad1d-bba2-410b-b072-4f8deb166b67" />

---

- C++17, Qt 5 Widgets and KDE Frameworks 5 (targets Ubuntu 24.04 / Plasma 5.27)
- Sign-in through the web browser (OAuth 2.0 with PKCE); the sign-in data is kept in the KDE wallet
- Several Drive folders, or the whole drive, can be mapped
- Sync on request and automatic
- Every deletion must be confirmed before anything is removed


> [!NOTE] 
> 100% vibe-coded by Claude Code and rigorously tested by @anjankow. 

- Confirmed working ✔️
- Confirmed no data lost ✔️
- Confirmed no shady work was done behind the scenes ✔️
- Confirmed no personal data is collected nor abused ✔️


## Install

Needs the KDE wallet service (`kwalletd5`) with a D-Bus session, a web browser for the sign-in, and a
systemd user session for automatic sync. After installing, do the
[Google Cloud setup](#google-cloud-setup-once) once.

The recommended way is to download the [release artifacts](https://github.com/anjankow/gdrive-sync/releases).
The scripts in `scripts/` are optional helpers if you prefer to clone the repository:

```bash
git clone https://github.com/anjankow/gdrive-sync.git && cd gdrive-sync
```

### Debian package (Ubuntu 24.04, amd64)

```bash
curl -fLO https://github.com/anjankow/gdrive-sync/releases/download/v0.1.0/gdrive-sync-0.1.0.deb && sudo apt install ./gdrive-sync-*.deb
```

Or from a clone: `scripts/install-deb.sh`

### AppImage (x86_64, glibc 2.39 or newer)

Runs without installing. Needs `libfuse2` (`sudo apt install libfuse2t64`).

```bash
curl -fLO https://github.com/anjankow/gdrive-sync/releases/download/v0.1.0/gdrive-sync-0.1.0.AppImage && chmod +x gdrive-sync-*.AppImage && ./gdrive-sync-*.AppImage
```

Or from a clone: `scripts/run-appimage.sh`

### Build from source

For other distributions or versions. Detailed instructions are in
[CONTRIBUTING.md](CONTRIBUTING.md#build-from-source).

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

1. **Sign In** with your google account via the browser.
2. **Add Folder**: pick a Drive folder, or **My Drive** for the whole drive, and a local destination. The destination may already hold files:
   the first sync uploads them to Drive (two-way and local to Drive; with Drive to local they stay local only).
   A file that exists on both sides with different content is asked about, in every direction.
3. Choose the sync direction of each folder in the **Direction** column (two-way, Drive to local, or local
   to Drive). New folders start with the default from **Settings → Default Sync Direction** (two-way
   unless changed).
4. **Sync Selected** / **Sync All**.
5. **Settings → Automatic Sync**: tick the box and choose the interval. This installs and starts
   `gdrive-sync.timer` in `~/.config/systemd/user`; unticking removes it again.


## Behavior

- Files deleted by a sync are moved to the trash (local trash and Drive trash), never destroyed directly.
- Automatic runs never delete anything and never resolve conflicts. They show a notification, and the next
  manual sync asks you.
- Google Docs, Sheets and Slides have no file content and are skipped.
- **Unmount** deletes the local copy of that folder. For a folder that already held files when it was
  added, you choose to keep all its local files or delete all of them. Google Drive is not touched.
- **Sign Out** revokes the sign-in at Google and forgets the folder list and the timer (if automatic sync is enabled). Local files stay.
