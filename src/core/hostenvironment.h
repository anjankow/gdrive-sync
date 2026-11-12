#pragma once

#include <QProcessEnvironment>

namespace gds {

/// The environment for programs the application starts (file manager, web
/// browser).
///
/// Run from an AppImage, the application's environment points into the
/// AppImage (APPDIR, QT_PLUGIN_PATH, XDG_DATA_DIRS, ...). A program started
/// with it would load the AppImage's Qt plugins: it may break, and it keeps
/// the AppImage mounted, so the AppImage process cannot end after its window
/// is closed. These entries are removed again here. Outside an AppImage the
/// environment is returned unchanged.
QProcessEnvironment hostEnvironment(
    const QProcessEnvironment& own = QProcessEnvironment::systemEnvironment());

} // namespace gds
