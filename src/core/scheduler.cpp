#include "scheduler.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace gds {

Scheduler::Scheduler(QString unitDir, QString executable, SystemctlRunner runner)
    : m_unitDir(std::move(unitDir))
    , m_executable(std::move(executable))
    , m_runner(runner ? std::move(runner) : SystemctlRunner(&Scheduler::runSystemctl))
{
}

QString Scheduler::defaultUnitDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/systemd/user");
}

QString Scheduler::serviceUnit(const QString& executable)
{
    QString quoted = executable;
    quoted.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QStringLiteral("[Unit]\n"
                          "Description=Google Drive Sync (automatic run)\n"
                          "After=graphical-session.target\n"
                          "\n"
                          "[Service]\n"
                          "Type=oneshot\n"
                          "ExecStart=\"%1\" --sync\n")
        .arg(quoted);
}

QString Scheduler::timerUnit(int intervalMinutes)
{
    return QStringLiteral("[Unit]\n"
                          "Description=Google Drive Sync timer\n"
                          "\n"
                          "[Timer]\n"
                          "OnStartupSec=2min\n"
                          "OnUnitActiveSec=%1min\n"
                          "Unit=%2.service\n"
                          "\n"
                          "[Install]\n"
                          "WantedBy=timers.target\n")
        .arg(qMax(1, intervalMinutes))
        .arg(unitName());
}

QString Scheduler::servicePath() const
{
    return m_unitDir + QLatin1Char('/') + unitName() + QStringLiteral(".service");
}

QString Scheduler::timerPath() const
{
    return m_unitDir + QLatin1Char('/') + unitName() + QStringLiteral(".timer");
}

bool Scheduler::isEnabled() const
{
    return QFile::exists(timerPath()) && QFile::exists(servicePath());
}

int Scheduler::intervalMinutes() const
{
    QFile f(timerPath());
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QRegularExpression re(QStringLiteral("^OnUnitActiveSec=(\\d+)min$"), QRegularExpression::MultilineOption);
    const auto match = re.match(QString::fromUtf8(f.readAll()));
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

static bool writeFile(const QString& path, const QString& content, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content.toUtf8()) < 0 || !f.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool Scheduler::enable(int intervalMinutes, QString* error)
{
    if (!QDir().mkpath(m_unitDir)) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(m_unitDir);
        return false;
    }
    if (!writeFile(servicePath(), serviceUnit(m_executable), error)
        || !writeFile(timerPath(), timerUnit(intervalMinutes), error))
        return false;
    const QString timer = unitName() + QStringLiteral(".timer");
    if (m_runner({ QStringLiteral("daemon-reload") }, error)
        && m_runner({ QStringLiteral("enable"), QStringLiteral("--now"), timer }, error)
        // apply a changed interval to an already running timer
        && m_runner({ QStringLiteral("restart"), timer }, error))
        return true;
    // do not leave a half-installed timer behind
    QFile::remove(timerPath());
    QFile::remove(servicePath());
    m_runner({ QStringLiteral("daemon-reload") }, nullptr);
    return false;
}

bool Scheduler::disable(QString* error)
{
    const QString timer = unitName() + QStringLiteral(".timer");
    bool ok = true;
    if (QFile::exists(timerPath()))
        ok = m_runner({ QStringLiteral("disable"), QStringLiteral("--now"), timer }, error);
    QFile::remove(timerPath());
    QFile::remove(servicePath());
    m_runner({ QStringLiteral("daemon-reload") }, nullptr);
    return ok;
}

bool Scheduler::runSystemctl(const QStringList& args, QString* error)
{
    QProcess p;
    p.start(QStringLiteral("systemctl"), QStringList { QStringLiteral("--user") } + args);
    if (!p.waitForFinished(15000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) {
            const QString out = QString::fromUtf8(p.readAllStandardError()).trimmed();
            *error = QStringLiteral("systemctl --user %1 failed: %2")
                         .arg(args.join(QLatin1Char(' ')), out.isEmpty() ? p.errorString() : out);
        }
        return false;
    }
    return true;
}

} // namespace gds
