#pragma once

#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gds {

enum class SyncDirection {
    Unset = 0,
    TwoWay = 1,
    DriveToLocal = 2,
    LocalToDrive = 3,
};

QString directionLabel(SyncDirection d);

struct LocalItem {
    bool isDir = false;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

struct RemoteItem {
    QString id;
    bool isDir = false;
    qint64 size = 0;
    QString md5;
    qint64 modifiedMs = 0;
};

/// What was known about a path after the last successful sync of it.
struct StateEntry {
    bool isDir = false;
    QString driveId;
    qint64 size = 0;
    QString md5;
    qint64 localMtimeMs = 0;
};

// QMap keeps paths sorted: parents always come before their children.
using LocalTree = QMap<QString, LocalItem>;
using RemoteTree = QMap<QString, RemoteItem>;
using StateMap = QMap<QString, StateEntry>;

enum class ActionType {
    RenameLocal, ///< path -> newPath (conflict copy)
    MkdirLocal,
    MkdirRemote,
    Download,
    Upload,
    DeleteLocal,
    DeleteRemote,
    Adopt, ///< both sides already equal: only record state
    Forget, ///< gone on both sides: drop state
    Conflict,
};

enum class ConflictKind {
    None,
    BothModified,
    LocalModifiedRemoteDeleted,
    RemoteModifiedLocalDeleted,
};

enum class ConflictChoice {
    Skip,
    KeepLocal,
    KeepRemote,
    KeepBoth,
};

struct SyncAction {
    ActionType type = ActionType::Adopt;
    QString path; ///< relative to the folder root, '/' separated
    bool isDir = false;
    qint64 bytes = 0; ///< bytes to transfer (Download/Upload)
    ConflictKind conflict = ConflictKind::None;
    QString newPath; ///< RenameLocal target
};

struct SyncPlan {
    QVector<SyncAction> actions;
    QStringList skipped; ///< human readable notes about items that are not synced

    qint64 transferBytes() const;
    int transferCount() const;
    QVector<SyncAction> conflicts() const;
    QStringList localDeletions() const;
    QStringList remoteDeletions() const;
    bool hasDeletions() const;
};

struct ConflictInfo {
    QString path;
    ConflictKind kind = ConflictKind::None;
};

struct SyncProgress {
    QString folderId;
    QString phase; ///< "scan", "transfer", "done"
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    int filesDone = 0;
    int filesTotal = 0;
    QString currentPath;

    qint64 bytesRemaining() const { return qMax<qint64>(0, bytesTotal - bytesDone); }
};

struct SyncResult {
    enum Status { Success, Cancelled, Failed, AuthRequired };
    Status status = Success;
    QString folderId;
    QString errorMessage;
    QStringList errors; ///< per-item failures
    QStringList skipped;
    int downloaded = 0;
    int uploaded = 0;
    int deletedLocal = 0;
    int deletedRemote = 0;
    qint64 bytesTransferred = 0;
    /// Unattended runs never delete or resolve conflicts; they are left pending.
    int pendingDeletions = 0;
    int pendingConflicts = 0;

    bool needsAttention() const { return pendingDeletions > 0 || pendingConflicts > 0; }
};

} // namespace gds

Q_DECLARE_METATYPE(gds::SyncDirection)
Q_DECLARE_METATYPE(gds::ConflictChoice)
using GdsConflictChoices = QHash<QString, gds::ConflictChoice>;
Q_DECLARE_METATYPE(GdsConflictChoices)
Q_DECLARE_METATYPE(gds::SyncProgress)
Q_DECLARE_METATYPE(gds::SyncResult)
Q_DECLARE_METATYPE(gds::ConflictInfo)
Q_DECLARE_METATYPE(QVector<gds::ConflictInfo>)
