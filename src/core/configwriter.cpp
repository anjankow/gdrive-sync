#include "configwriter.h"

#include "logger.h"

#include <KConfigGroup>

#include <mutex>

namespace gds {

ConfigWriter::ConfigWriter(KSharedConfig::Ptr config)
    : m_config(std::move(config))
    , m_file(m_config->name())
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

ConfigWriter::~ConfigWriter()
{
    flush();
}

void ConfigWriter::collect(const KConfigGroup& group, const QStringList& path, Snapshot& out)
{
    out.insert(path, group.entryMap());
    const QStringList children = group.groupList();
    for (const QString& child : children)
        collect(group.group(child), path + QStringList {child}, out);
}

void ConfigWriter::save()
{
    Snapshot snapshot;
    const QStringList groups = m_config->groupList();
    for (const QString& name : groups)
        collect(m_config->group(name), {name}, snapshot);
    // the in-memory object must never write the file itself (e.g. when destroyed)
    m_config->markAsClean();

    const std::lock_guard<QMutex> lock(m_mutex);
    const bool scheduled = m_pending.has_value();
    m_pending = std::move(snapshot);
    if (scheduled)
        return; // the queued write picks up the newer content
    m_pool.start([this]() {
        std::optional<Snapshot> next;
        {
            const std::lock_guard<QMutex> lock(m_mutex);
            next.swap(m_pending);
        }
        if (next)
            writeSnapshot(m_file, *next);
    });
}

void ConfigWriter::flush()
{
    m_pool.waitForDone();
}

void ConfigWriter::writeSnapshot(const QString& file, const Snapshot& snapshot)
{
    KConfig config(file, KConfig::SimpleConfig);
    const QStringList existing = config.groupList();
    for (const QString& name : existing)
        config.deleteGroup(name);
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        KConfigGroup group = config.group(it.key().first());
        for (int i = 1; i < it.key().size(); ++i)
            group = group.group(it.key().at(i));
        for (auto entry = it.value().cbegin(); entry != it.value().cend(); ++entry)
            group.writeEntry(entry.key(), entry.value());
    }
    if (!config.sync())
        logError(QStringLiteral("The settings could not be written to %1").arg(file));
}

} // namespace gds
