# Sourced by the AppRun of the AppImage before gdrive-sync starts.
# KDE looks for the notification settings (knotifications5/gdrive-sync.notifyrc)
# and the application icon in XDG_DATA_DIRS; the host's directories stay
# behind them so that the desktop's icon theme is still found.
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
