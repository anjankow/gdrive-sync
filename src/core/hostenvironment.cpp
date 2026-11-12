#include "hostenvironment.h"

#include <QDir>
#include <QStringList>

namespace gds {

QProcessEnvironment hostEnvironment(const QProcessEnvironment& own)
{
    const QString appDir = own.value(QStringLiteral("APPDIR"));
    if (appDir.isEmpty())
        return own;
    QProcessEnvironment env = own;
    const QString root = QDir::cleanPath(appDir);
    const auto insideAppImage = [&root](const QString& path) {
        const QString clean = QDir::cleanPath(path);
        return clean == root || clean.startsWith(root + QLatin1Char('/'));
    };
    // lists of directories: drop the AppImage's own entries
    for (const char* name : {"PATH", "LD_LIBRARY_PATH", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS", "QT_PLUGIN_PATH",
             "QML2_IMPORT_PATH", "GST_PLUGIN_SYSTEM_PATH", "GSETTINGS_SCHEMA_DIR"}) {
        const QString key = QString::fromLatin1(name);
        if (!env.contains(key))
            continue;
        QStringList kept;
        const QStringList parts = env.value(key).split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (!insideAppImage(part))
                kept.append(part);
        }
        if (kept.isEmpty())
            env.remove(key);
        else
            env.insert(key, kept.join(QLatin1Char(':')));
    }
    // set by the AppImage runtime and AppRun for the application only
    for (const char* name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD", "QT_QPA_PLATFORM_PLUGIN_PATH"})
        env.remove(QString::fromLatin1(name));
    return env;
}

} // namespace gds
