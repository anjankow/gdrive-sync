#include "syncstate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace gds {

SyncState::SyncState(QString filePath)
    : m_filePath(std::move(filePath))
{
}

bool SyncState::load(QString* error)
{
    m_entries.clear();
    QFile f(m_filePath);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonArray items = doc.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        StateEntry e;
        e.isDir = o.value(QStringLiteral("dir")).toBool();
        e.driveId = o.value(QStringLiteral("id")).toString();
        e.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
        e.md5 = o.value(QStringLiteral("md5")).toString();
        e.localMtimeMs = o.value(QStringLiteral("mtime")).toVariant().toLongLong();
        m_entries.insert(o.value(QStringLiteral("path")).toString(), e);
    }
    return true;
}

bool SyncState::save(QString* error) const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QJsonArray items;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), it.key());
        o.insert(QStringLiteral("id"), it->driveId);
        if (it->isDir) {
            o.insert(QStringLiteral("dir"), true);
        } else {
            o.insert(QStringLiteral("size"), QString::number(it->size));
            o.insert(QStringLiteral("md5"), it->md5);
            o.insert(QStringLiteral("mtime"), QString::number(it->localMtimeMs));
        }
        items.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), items);
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool SyncState::removeFile(const QString& filePath)
{
    return !QFile::exists(filePath) || QFile::remove(filePath);
}

} // namespace gds
