#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace gds {

/// Installs the automatic sync as a systemd user timer. Nothing is installed
/// or started until enable() is called.
class Scheduler {
public:
    /// Runs "systemctl --user <args>"; returns false and sets error on failure.
    using SystemctlRunner = std::function<bool(const QStringList& args, QString* error)>;

    Scheduler(QString unitDir, QString executable, SystemctlRunner runner = {});

    static QString unitName() { return QStringLiteral("gdrive-sync"); }
    static QString serviceUnit(const QString& executable);
    static QString timerUnit(int intervalMinutes);
    /// ~/.config/systemd/user
    static QString defaultUnitDir();

    bool isEnabled() const;
    /// Interval of the installed timer in minutes, 0 when not installed.
    int intervalMinutes() const;

    /// Writes the units and starts the timer. Blocking (runs systemctl).
    bool enable(int intervalMinutes, QString* error = nullptr);
    /// Stops the timer and removes the units. Blocking (runs systemctl).
    bool disable(QString* error = nullptr);

    QString servicePath() const;
    QString timerPath() const;

    static bool runSystemctl(const QStringList& args, QString* error);

private:
    QString m_unitDir;
    QString m_executable;
    SystemctlRunner m_runner;
};

} // namespace gds
