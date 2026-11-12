#pragma once

#include <KSharedConfig>

#include <QMap>
#include <QMutex>
#include <QStringList>
#include <QThreadPool>

#include <optional>

namespace gds {

/// Writes the settings file in the background.
///
/// KConfig::sync() commits through QSaveFile, which waits for the disk
/// (fdatasync). On a busy disk that wait takes hundreds of milliseconds, so it
/// must not happen in the GUI thread. The GUI thread keeps reading and changing
/// its in-memory KConfig; save() hands a copy of all entries to a worker thread,
/// which writes them with a KConfig object of its own. Writes happen in order,
/// and when several are pending only the newest one is written.
class ConfigWriter {
public:
    explicit ConfigWriter(KSharedConfig::Ptr config);
    ~ConfigWriter();
    ConfigWriter(const ConfigWriter&) = delete;
    ConfigWriter& operator=(const ConfigWriter&) = delete;

    /// Schedules writing the current content of the in-memory config.
    void save();
    /// Waits until everything scheduled so far is on disk.
    void flush();

private:
    /// group path (outermost first) -> entries of that group
    using Snapshot = QMap<QStringList, QMap<QString, QString>>;
    static void collect(const KConfigGroup& group, const QStringList& path, Snapshot& out);
    static void writeSnapshot(const QString& file, const Snapshot& snapshot);

    KSharedConfig::Ptr m_config;
    QString m_file;
    QThreadPool m_pool; // one thread: writes stay in order
    QMutex m_mutex;
    std::optional<Snapshot> m_pending;
};

} // namespace gds
