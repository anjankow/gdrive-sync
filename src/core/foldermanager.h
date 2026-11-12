#pragma once

#include "configwriter.h"
#include "integration.h"

#include <KSharedConfig>

#include <QDateTime>
#include <QObject>
#include <QVector>

namespace gds {

/// One Drive folder mapped ("mounted") to one local directory.
struct FolderConfig {
    QString id;
    QString driveFolderId;
    QString driveFolderName;
    QString localPath;
    QDateTime lastSync;
    QString lastStatus;
    /// The local folder already held files when it was added: unmounting
    /// then lets the user keep the local files.
    bool initiallyNonEmpty = false;
};

/// Manages the list of mounted folders. Each folder is independent of the others.
class FolderManager : public QObject {
    Q_OBJECT
public:
    FolderManager(KSharedConfig::Ptr config, ConfigWriter* writer, QString stateDir,
        PlacesIntegration* places, QObject* parent = nullptr);

    QVector<FolderConfig> folders() const { return m_folders; }
    const FolderConfig* find(const QString& id) const;
    QString stateFile(const QString& id) const;

    /// Returns an empty string when \p path can be used as a mount
    /// destination, otherwise the reason why not. The destination must be an
    /// empty (or not yet existing) directory outside the other mounts.
    QString validateDestination(const QString& path) const;

    /// Mounts a Drive folder. Returns the new folder id, or empty on error.
    QString addFolder(const QString& driveFolderId, const QString& driveFolderName, const QString& localPath,
        QString* error);

    /// Unmounts a folder: the local copy is deleted (in a background
    /// thread) unless \p deleteLocalFiles is false; Drive is not touched.
    /// Emits folderRemoved() when done.
    void removeFolder(const QString& id, bool deleteLocalFiles = true);

    /// Forgets all folders without deleting local files (used on logout).
    void clear();

    void recordSync(const QString& id, const QString& status);

Q_SIGNALS:
    void foldersChanged();
    void folderRemoved(const QString& id, bool ok, const QString& error);

private:
    void load();
    void save();

    KSharedConfig::Ptr m_config;
    ConfigWriter* m_writer;
    QString m_stateDir;
    PlacesIntegration* m_places;
    QVector<FolderConfig> m_folders;
};

} // namespace gds
