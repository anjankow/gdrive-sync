#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace gds {

enum class LogLevel {
    Debug, ///< file syncs
    Info, ///< sign-in/sign-out and configuration changes
    Warn, ///< sync errors
    Error, ///< unexpected application-level errors
};

/// Application log file. Thread-safe: the sync worker and the GUI thread
/// both write to it. Nothing is written until a directory was set.
class Logger {
public:
    static Logger& instance();

    static QString fileName() { return QStringLiteral("gdrive-sync.log"); }
    /// A private directory below the system temp directory, e.g. /tmp/gdrive-sync-<user>.
    static QString defaultDirectory();
    static QString levelName(LogLevel level);

    /// Opens (appends to) gdrive-sync.log in \p directory, creating the
    /// directory when needed. On failure the previous log file stays in use.
    bool setDirectory(const QString& directory, QString* error = nullptr);
    QString directory() const;
    QString filePath() const;
    void close();

    void write(LogLevel level, const QString& message);

    /// Formats one line: "2026-01-31T12:00:00.000+01:00 INFO  message".
    static QString formatLine(LogLevel level, const QString& message, const QDateTime& time);

private:
    Logger() = default;
    void rotateIfLarge(const QString& path);

    mutable QMutex m_mutex;
    QFile m_file;
    QString m_directory;
};

void logDebug(const QString& message);
void logInfo(const QString& message);
void logWarn(const QString& message);
void logError(const QString& message);

} // namespace gds
