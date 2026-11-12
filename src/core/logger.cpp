#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

namespace gds {

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

QString Logger::defaultDirectory()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("LOGNAME", QStringLiteral("user"));
    return QDir::tempPath() + QStringLiteral("/gdrive-sync-") + user;
}

QString Logger::levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warn:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString Logger::formatLine(LogLevel level, const QString& message, const QDateTime& time)
{
    QString text = message;
    // one event per line
    text.replace(QLatin1Char('\n'), QLatin1String("\\n")).replace(QLatin1Char('\r'), QString());
    // local time with a numeric UTC offset, also when the offset is zero
    const int offset = time.offsetFromUtc();
    const QString stamp = time.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"))
        + QStringLiteral("%1%2:%3")
              .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
              .arg(qAbs(offset) / 3600, 2, 10, QLatin1Char('0'))
              .arg((qAbs(offset) % 3600) / 60, 2, 10, QLatin1Char('0'));
    return stamp + QLatin1Char(' ') + levelName(level).leftJustified(5) + QLatin1Char(' ') + text
        + QLatin1Char('\n');
}

/// Drops the oldest entries once the file reached its maximum size, so that
/// the newest ones always fit. The file is rewritten in place (same inode):
/// another gdrive-sync process appending to it keeps working.
/// Called with m_mutex held.
void Logger::trimIfLarge()
{
    if (m_file.size() <= maxBytes())
        return;
    QFile file(m_file.fileName());
    if (!file.open(QIODevice::ReadWrite))
        return;
    // keep the newest three quarters; cutting a batch avoids rewriting the file for every line
    const qint64 keep = maxBytes() * 3 / 4;
    if (!file.seek(file.size() - keep))
        return;
    QByteArray tail = file.readAll();
    const int firstLineEnd = tail.indexOf('\n');
    tail = firstLineEnd >= 0 ? tail.mid(firstLineEnd + 1) : QByteArray(); // start at a whole entry
    if (file.seek(0) && file.write(tail) == tail.size())
        file.resize(tail.size());
}

bool Logger::setDirectory(const QString& directory, QString* error)
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (directory.trimmed().isEmpty() || !QDir::isAbsolutePath(directory))
        return fail(QStringLiteral("The log location must be an absolute folder path."));
    const QString clean = QDir::cleanPath(directory);

    QMutexLocker lock(&m_mutex);
    if (clean == m_directory && m_file.isOpen())
        return true;
    const bool existed = QFileInfo::exists(clean);
    if (!QDir().mkpath(clean))
        return fail(QStringLiteral("The folder %1 cannot be created.").arg(clean));
    if (!existed) // a folder we create is private: the log contains file names
        QFile::setPermissions(
            clean, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString path = clean + QLatin1Char('/') + fileName();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return fail(QStringLiteral("Cannot write %1: %2").arg(path, file.errorString()));
    file.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    m_file.close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_directory.clear();
        return fail(QStringLiteral("Cannot write %1: %2").arg(path, m_file.errorString()));
    }
    m_directory = clean;
    trimIfLarge();
    return true;
}

QString Logger::directory() const
{
    QMutexLocker lock(&m_mutex);
    return m_directory;
}

QString Logger::filePath() const
{
    QMutexLocker lock(&m_mutex);
    return m_file.isOpen() ? m_file.fileName() : QString();
}

void Logger::close()
{
    QMutexLocker lock(&m_mutex);
    m_file.close();
    m_directory.clear();
}

void Logger::write(LogLevel level, const QString& message)
{
    const QByteArray line = formatLine(level, message, QDateTime::currentDateTime()).toUtf8();
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen())
        return;
    m_file.write(line);
    m_file.flush(); // the log must be complete even after a crash
    trimIfLarge();
}

void logDebug(const QString& message)
{
    Logger::instance().write(LogLevel::Debug, message);
}

void logInfo(const QString& message)
{
    Logger::instance().write(LogLevel::Info, message);
}

void logWarn(const QString& message)
{
    Logger::instance().write(LogLevel::Warn, message);
}

void logError(const QString& message)
{
    Logger::instance().write(LogLevel::Error, message);
}

} // namespace gds
