#pragma once

#include "driveclient.h"
#include "endpoints.h"
#include "synctypes.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

#include <atomic>
#include <memory>

namespace gds {

class TokenSource;
class SyncState;

struct SyncJob {
    QString folderId; ///< id of the folder mapping (not the Drive id)
    QString driveFolderId;
    QString localPath;
    QString stateFile;
    SyncDirection direction = SyncDirection::Unset;
    /// Interactive runs ask about conflicts and deletions. Unattended runs
    /// skip both and report them as pending.
    bool interactive = true;
};

/// Runs the sync of one folder. Lives in a worker thread: run() blocks that
/// thread, the GUI thread only exchanges signals with it.
class SyncEngine : public QObject {
    Q_OBJECT
public:
    SyncEngine(GoogleEndpoints endpoints, std::shared_ptr<TokenSource> tokens, QObject* parent = nullptr);
    ~SyncEngine() override;

    // Thread-safe; may be called from the GUI thread while run() is active.
    void requestCancel();
    void resetCancel();
    void provideConflictChoices(const QHash<QString, gds::ConflictChoice>& choices);
    void provideDeletionDecision(bool proceed);

    void setRetryBaseDelayMs(int ms) { m_retryBaseMs = ms; }

public Q_SLOTS:
    void run(const gds::SyncJob& job);

Q_SIGNALS:
    void progress(const gds::SyncProgress& progress);
    /// Emitted (interactive runs) when files changed on both sides.
    /// Answer with provideConflictChoices().
    void conflictsFound(const QString& folderId, const QVector<gds::ConflictInfo>& conflicts);
    /// Emitted (interactive runs) before anything is deleted.
    /// Answer with provideDeletionDecision().
    void deletionsPending(const QString& folderId, const QStringList& localPaths, const QStringList& remotePaths);
    void finished(const gds::SyncResult& result);

private:
    struct Context;
    SyncResult doRun(const SyncJob& job);
    DriveError listRemote(Context& ctx);
    bool execute(Context& ctx, const SyncAction& action);
    QString ensureRemoteDir(Context& ctx, const QString& path, DriveError* error);
    void emitProgress(Context& ctx, qint64 currentBytes, const QString& path, bool force = false);
    bool waitForAnswer(bool& flag);

    GoogleEndpoints m_endpoints;
    std::shared_ptr<TokenSource> m_tokens;
    std::atomic_bool m_cancel { false };
    int m_retryBaseMs = 500;

    QMutex m_mutex;
    QWaitCondition m_wait;
    bool m_haveChoices = false;
    QHash<QString, ConflictChoice> m_choices;
    bool m_haveDecision = false;
    bool m_proceedWithDeletions = false;
    QElapsedTimer m_lastProgress;
};

} // namespace gds

Q_DECLARE_METATYPE(gds::SyncJob)
