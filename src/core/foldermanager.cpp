#include "foldermanager.h"

#include "logger.h"
#include "syncstate.h"

#include <KConfigGroup>

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QUuid>
#include <QtConcurrent>

namespace gds {

namespace {
    QString normalized(const QString& path)
    {
        return QDir::cleanPath(QDir(path).absolutePath());
    }
    bool isSameOrInside(const QString& path, const QString& dir)
    {
        return path == dir || path.startsWith(dir + QLatin1Char('/'));
    }
}

FolderManager::FolderManager(KSharedConfig::Ptr config, ConfigWriter* writer, QString stateDir,
    PlacesIntegration* places, QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_writer(writer)
    , m_stateDir(std::move(stateDir))
    , m_places(places)
{
    load();
    migrateAccountDirection();
}

namespace {
    SyncDirection parseDirection(int value, SyncDirection fallback)
    {
        return value >= 1 && value <= 3 ? SyncDirection(value) : fallback;
    }
}

/// Earlier versions had one direction for the whole sign-in session
/// ("Account/direction"): it becomes the default and the direction of the
/// folders that had none.
void FolderManager::migrateAccountDirection()
{
    KConfigGroup account = m_config->group("Account");
    if (!account.hasKey("direction"))
        return;
    const SyncDirection old = parseDirection(account.readEntry("direction", 0), SyncDirection::TwoWay);
    KConfigGroup sync = m_config->group("Sync");
    if (!sync.hasKey("defaultDirection"))
        sync.writeEntry("defaultDirection", int(old));
    const KConfigGroup root = m_config->group("Folders");
    for (FolderConfig& f : m_folders) {
        if (!root.group(f.id).hasKey("direction"))
            f.direction = old;
    }
    account.deleteEntry("direction");
    save();
}

SyncDirection FolderManager::defaultDirection() const
{
    return parseDirection(m_config->group("Sync").readEntry("defaultDirection", 0), SyncDirection::TwoWay);
}

void FolderManager::setDefaultDirection(SyncDirection direction)
{
    m_config->group("Sync").writeEntry("defaultDirection", int(direction));
    m_writer->save();
    logInfo(
        QStringLiteral("Default sync direction for new folders set to %1").arg(directionLabel(direction)));
}

bool FolderManager::setDirection(const QString& id, SyncDirection direction)
{
    for (FolderConfig& f : m_folders) {
        if (f.id != id)
            continue;
        if (f.direction == direction)
            return true;
        f.direction = direction;
        save();
        logInfo(QStringLiteral("Sync direction of \"%1\" (%2) set to %3")
                    .arg(f.driveFolderName, f.localPath, directionLabel(direction)));
        Q_EMIT foldersChanged();
        return true;
    }
    return false;
}

void FolderManager::load()
{
    m_folders.clear();
    const KConfigGroup root = m_config->group("Folders");
    const QStringList ids = root.readEntry("ids", QStringList());
    for (const QString& id : ids) {
        const KConfigGroup g = root.group(id);
        FolderConfig f;
        f.id = id;
        f.driveFolderId = g.readEntry("driveFolderId", QString());
        f.driveFolderName = g.readEntry("driveFolderName", QString());
        f.localPath = g.readEntry("localPath", QString());
        f.lastSync = g.readEntry("lastSync", QDateTime());
        f.lastStatus = g.readEntry("lastStatus", QString());
        f.initiallyNonEmpty = g.readEntry("initiallyNonEmpty", false);
        f.direction = parseDirection(g.readEntry("direction", 0), SyncDirection::TwoWay);
        if (!f.driveFolderId.isEmpty() && !f.localPath.isEmpty())
            m_folders.append(f);
    }
}

void FolderManager::save()
{
    m_config->deleteGroup("Folders");
    KConfigGroup root = m_config->group("Folders");
    QStringList ids;
    for (const FolderConfig& f : qAsConst(m_folders)) {
        ids.append(f.id);
        KConfigGroup g = root.group(f.id);
        g.writeEntry("driveFolderId", f.driveFolderId);
        g.writeEntry("driveFolderName", f.driveFolderName);
        g.writeEntry("localPath", f.localPath);
        g.writeEntry("lastSync", f.lastSync);
        g.writeEntry("lastStatus", f.lastStatus);
        if (f.initiallyNonEmpty)
            g.writeEntry("initiallyNonEmpty", true);
        g.writeEntry("direction", int(f.direction));
    }
    root.writeEntry("ids", ids);
    m_writer->save();
}

const FolderConfig* FolderManager::find(const QString& id) const
{
    for (const FolderConfig& f : m_folders) {
        if (f.id == id)
            return &f;
    }
    return nullptr;
}

QString FolderManager::stateFile(const QString& id) const
{
    return m_stateDir + QLatin1Char('/') + id + QStringLiteral(".json");
}

QString FolderManager::validateDestination(const QString& path) const
{
    if (path.trimmed().isEmpty())
        return QStringLiteral("No destination folder selected.");
    if (!QDir::isAbsolutePath(path))
        return QStringLiteral("The destination must be an absolute path.");
    const QString clean = normalized(path);
    const QFileInfo info(clean);
    if (info.exists()) {
        if (!info.isDir())
            return QStringLiteral("The destination is not a folder.");
        if (!info.isWritable())
            return QStringLiteral("The destination folder is not writable.");
    } else {
        QFileInfo parent(info.absolutePath());
        while (!parent.exists() && parent.absoluteFilePath() != parent.absolutePath())
            parent = QFileInfo(parent.absolutePath());
        if (!parent.isDir() || !parent.isWritable())
            return QStringLiteral("The destination folder cannot be created.");
    }
    for (const FolderConfig& f : m_folders) {
        const QString other = normalized(f.localPath);
        if (isSameOrInside(clean, other) || isSameOrInside(other, clean))
            return QStringLiteral("The destination overlaps with the already mounted folder %1.")
                .arg(f.localPath);
    }
    return {};
}

QString FolderManager::addFolder(
    const QString& driveFolderId, const QString& driveFolderName, const QString& localPath, QString* error)
{
    const auto setError = [error](const QString& e) {
        if (error)
            *error = e;
        return QString();
    };
    if (driveFolderId.isEmpty())
        return setError(QStringLiteral("No Google Drive folder selected."));
    for (const FolderConfig& f : qAsConst(m_folders)) {
        if (f.driveFolderId == driveFolderId)
            return setError(
                QStringLiteral("This Google Drive folder is already mounted at %1.").arg(f.localPath));
        // My Drive contains every folder: the same files would be synced twice
        if (f.driveFolderId == rootFolderId())
            return setError(
                QStringLiteral("The whole of My Drive is already mounted at %1, and it contains this folder.")
                    .arg(f.localPath));
        if (driveFolderId == rootFolderId())
            return setError(QStringLiteral("My Drive contains the already mounted folder \"%1\". Unmount the "
                                           "other folders first to mount the whole drive.")
                                .arg(f.driveFolderName));
    }
    const QString problem = validateDestination(localPath);
    if (!problem.isEmpty())
        return setError(problem);
    const QString clean = normalized(localPath);
    // its files are synced too: uploaded on the first sync (two-way, local to Drive)
    const bool nonEmpty = QFileInfo(clean).isDir()
        && !QDir(clean).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (!QDir().mkpath(clean))
        return setError(QStringLiteral("The destination folder cannot be created."));

    FolderConfig f;
    f.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    f.driveFolderId = driveFolderId;
    f.driveFolderName = driveFolderName;
    f.localPath = clean;
    f.initiallyNonEmpty = nonEmpty;
    f.direction = defaultDirection();
    m_folders.append(f);
    save();
    if (m_places)
        m_places->addPlace(clean, driveFolderName);
    logInfo(QStringLiteral("Folder added: Drive folder \"%1\" mounted at %2%3")
                .arg(driveFolderName, clean,
                    nonEmpty ? QStringLiteral(" (the folder already holds files; they are synced too)")
                             : QString()));
    Q_EMIT foldersChanged();
    return f.id;
}

void FolderManager::removeFolder(const QString& id, bool deleteLocalFiles)
{
    const FolderConfig* found = find(id);
    if (!found) {
        Q_EMIT folderRemoved(id, false, QStringLiteral("Unknown folder"));
        return;
    }
    const FolderConfig folder = *found;
    for (int i = 0; i < m_folders.size(); ++i) {
        if (m_folders.at(i).id == id) {
            m_folders.remove(i);
            break;
        }
    }
    save();
    if (m_places)
        m_places->removePlace(folder.localPath);
    logInfo(deleteLocalFiles ? QStringLiteral("Folder unmounted: \"%1\"; deleting the local copy in %2")
                                   .arg(folder.driveFolderName, folder.localPath)
                             : QStringLiteral("Folder unmounted: \"%1\"; the local files in %2 are kept")
                                   .arg(folder.driveFolderName, folder.localPath));
    Q_EMIT foldersChanged();

    // Deleting a large tree can take long: keep it off the GUI thread.
    const QString stateFilePath = stateFile(id);
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, id, folder]() {
        const bool ok = watcher->result();
        watcher->deleteLater();
        Q_EMIT folderRemoved(id, ok,
            ok ? QString() : QStringLiteral("Some files in %1 could not be deleted.").arg(folder.localPath));
    });
    watcher->setFuture(QtConcurrent::run([path = folder.localPath, stateFilePath, deleteLocalFiles]() {
        SyncState::removeFile(stateFilePath);
        if (!deleteLocalFiles)
            return true;
        QDir dir(path);
        return !dir.exists() || dir.removeRecursively();
    }));
}

void FolderManager::clear()
{
    for (const FolderConfig& f : qAsConst(m_folders)) {
        SyncState::removeFile(stateFile(f.id));
        if (m_places)
            m_places->removePlace(f.localPath);
    }
    m_folders.clear();
    save();
    Q_EMIT foldersChanged();
}

void FolderManager::recordSync(const QString& id, const QString& status)
{
    for (FolderConfig& f : m_folders) {
        if (f.id == id) {
            f.lastSync = QDateTime::currentDateTime();
            f.lastStatus = status;
            save();
            Q_EMIT foldersChanged();
            return;
        }
    }
}

} // namespace gds
