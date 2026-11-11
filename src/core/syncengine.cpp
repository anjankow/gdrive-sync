#include "syncengine.h"

#include "localscanner.h"
#include "planner.h"
#include "syncstate.h"
#include "tokensource.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

namespace gds {

struct SyncEngine::Context {
    SyncJob job;
    std::unique_ptr<DriveClient> client;
    std::unique_ptr<SyncState> state;
    LocalTree local;
    RemoteTree remote;
    QHash<QString, QString> remoteDirIds; ///< relative dir path -> Drive id ("" = root)
    QStringList unsyncedRemote; ///< remote paths that exist but are not synced
    QHash<QString, QString> md5Cache;
    SyncResult result;
    qint64 bytesTotal = 0;
    qint64 bytesBase = 0; ///< bytes of completed transfers
    int filesTotal = 0;
    int filesDone = 0;

    QString abs(const QString& rel) const { return job.localPath + QLatin1Char('/') + rel; }
};

namespace {
    QString parentPath(const QString& path)
    {
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        return slash < 0 ? QString() : path.left(slash);
    }
    QString baseName(const QString& path) { return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1); }
    bool isUnder(const QString& path, const QString& dir)
    {
        return path.size() > dir.size() && path.startsWith(dir) && path.at(dir.size()) == QLatin1Char('/');
    }
    bool validName(const QString& name)
    {
        return !name.isEmpty() && !name.contains(QLatin1Char('/')) && name != QLatin1String(".")
            && name != QLatin1String("..") && !name.contains(QChar(0));
    }
}

SyncEngine::SyncEngine(GoogleEndpoints endpoints, std::shared_ptr<TokenSource> tokens, QObject* parent)
    : QObject(parent)
    , m_endpoints(std::move(endpoints))
    , m_tokens(std::move(tokens))
{
    qRegisterMetaType<gds::SyncJob>();
    qRegisterMetaType<gds::SyncProgress>();
    qRegisterMetaType<gds::SyncResult>();
    qRegisterMetaType<QVector<gds::ConflictInfo>>();
}

SyncEngine::~SyncEngine() = default;

void SyncEngine::requestCancel()
{
    m_cancel.store(true);
    QMutexLocker lock(&m_mutex);
    m_wait.wakeAll();
}

void SyncEngine::resetCancel()
{
    m_cancel.store(false);
}

void SyncEngine::provideConflictChoices(const QHash<QString, ConflictChoice>& choices)
{
    QMutexLocker lock(&m_mutex);
    m_choices = choices;
    m_haveChoices = true;
    m_wait.wakeAll();
}

void SyncEngine::provideDeletionDecision(bool proceed)
{
    QMutexLocker lock(&m_mutex);
    m_proceedWithDeletions = proceed;
    m_haveDecision = true;
    m_wait.wakeAll();
}

/// Blocks the worker thread until the GUI answered or the run was cancelled.
bool SyncEngine::waitForAnswer(bool& flag)
{
    QMutexLocker lock(&m_mutex);
    while (!flag && !m_cancel.load())
        m_wait.wait(&m_mutex, 100);
    return flag;
}

void SyncEngine::emitProgress(Context& ctx, qint64 currentBytes, const QString& path, bool force)
{
    // Throttled so that the GUI thread is never flooded with events.
    if (!force && m_lastProgress.isValid() && m_lastProgress.elapsed() < 50)
        return;
    m_lastProgress.restart();
    SyncProgress p;
    p.folderId = ctx.job.folderId;
    p.phase = QStringLiteral("transfer");
    p.bytesTotal = ctx.bytesTotal;
    p.bytesDone = qMin(ctx.bytesTotal, ctx.bytesBase + qMax<qint64>(0, currentBytes));
    p.filesTotal = ctx.filesTotal;
    p.filesDone = ctx.filesDone;
    p.currentPath = path;
    Q_EMIT progress(p);
}

void SyncEngine::run(const SyncJob& job)
{
    {
        QMutexLocker lock(&m_mutex);
        m_haveChoices = false;
        m_haveDecision = false;
        m_choices.clear();
    }
    SyncResult result = doRun(job);
    result.folderId = job.folderId;
    Q_EMIT finished(result);
}

DriveError SyncEngine::listRemote(Context& ctx)
{
    ctx.remoteDirIds.insert(QString(), ctx.job.driveFolderId);
    QStringList queue { QString() };
    while (!queue.isEmpty()) {
        const QString dir = queue.takeFirst();
        const auto children = ctx.client->listChildren(ctx.remoteDirIds.value(dir));
        if (!children.ok())
            return children.error;
        for (const DriveItem& item : children.value) {
            const QString path = dir.isEmpty() ? item.name : dir + QLatin1Char('/') + item.name;
            if (!validName(item.name)) {
                ctx.unsyncedRemote.append(dir.isEmpty() ? QStringLiteral("?") : dir + QStringLiteral("/?"));
                ctx.result.skipped.append(
                    QStringLiteral("%1: name cannot be used on the local file system").arg(path));
                continue;
            }
            if (item.isGoogleNative()) {
                ctx.unsyncedRemote.append(path);
                ctx.result.skipped.append(QStringLiteral("%1: Google document, not synced").arg(path));
                continue;
            }
            if (ctx.remote.contains(path)) {
                ctx.unsyncedRemote.append(path);
                ctx.result.skipped.append(
                    QStringLiteral("%1: several Drive items share this name; only the first is synced").arg(path));
                continue;
            }
            RemoteItem r;
            r.id = item.id;
            r.isDir = item.isFolder();
            r.size = item.size;
            r.md5 = item.md5;
            r.modifiedMs = item.modifiedMs;
            if (!r.isDir && r.md5.isEmpty()) {
                ctx.unsyncedRemote.append(path);
                ctx.result.skipped.append(QStringLiteral("%1: has no downloadable content").arg(path));
                continue;
            }
            ctx.remote.insert(path, r);
            if (r.isDir) {
                ctx.remoteDirIds.insert(path, r.id);
                queue.append(path);
            }
        }
    }
    return {};
}

QString SyncEngine::ensureRemoteDir(Context& ctx, const QString& path, DriveError* error)
{
    const auto it = ctx.remoteDirIds.constFind(path);
    if (it != ctx.remoteDirIds.cend())
        return it.value();
    const QString parentId = ensureRemoteDir(ctx, parentPath(path), error);
    if (parentId.isEmpty())
        return {};
    const auto created = ctx.client->createFolder(baseName(path), parentId);
    if (!created.ok()) {
        *error = created.error;
        return {};
    }
    ctx.remoteDirIds.insert(path, created.value.id);
    StateEntry e;
    e.isDir = true;
    e.driveId = created.value.id;
    ctx.state->entries().insert(path, e);
    return created.value.id;
}

SyncResult SyncEngine::doRun(const SyncJob& job)
{
    Context ctx;
    ctx.job = job;
    SyncResult& result = ctx.result;
    const auto fail = [&](SyncResult::Status status, const QString& message) {
        result.status = status;
        result.errorMessage = message;
        return result;
    };
    const auto failFrom = [&](const DriveError& e) {
        if (e.kind == DriveError::Cancelled)
            return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));
        if (e.kind == DriveError::Auth)
            return fail(SyncResult::AuthRequired, e.message);
        return fail(SyncResult::Failed, e.message);
    };

    if (m_cancel.load())
        return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));
    if (job.direction == SyncDirection::Unset)
        return fail(SyncResult::Failed, QStringLiteral("No sync direction selected"));
    // Never interpret a missing local folder as "everything was deleted".
    if (!QFileInfo(job.localPath).isDir())
        return fail(SyncResult::Failed, QStringLiteral("The local folder %1 does not exist").arg(job.localPath));

    ctx.client = std::make_unique<DriveClient>(m_endpoints, m_tokens, &m_cancel);
    ctx.client->setRetryBaseDelayMs(m_retryBaseMs);
    ctx.state = std::make_unique<SyncState>(job.stateFile);
    QString stateError;
    if (!ctx.state->load(&stateError))
        return fail(SyncResult::Failed, QStringLiteral("Cannot read the sync state: %1").arg(stateError));

    SyncProgress scanning;
    scanning.folderId = job.folderId;
    scanning.phase = QStringLiteral("scan");
    Q_EMIT progress(scanning);

    if (const DriveError e = listRemote(ctx))
        return failFrom(e);
    QStringList localSkipped;
    ctx.local = scanLocalTree(job.localPath, &localSkipped, &m_cancel);
    result.skipped += localSkipped;
    if (m_cancel.load())
        return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));

    const Md5Provider md5 = [&](const QString& rel) {
        auto it = ctx.md5Cache.find(rel);
        if (it == ctx.md5Cache.end())
            it = ctx.md5Cache.insert(rel, fileMd5(ctx.abs(rel), &m_cancel));
        return it.value();
    };
    SyncPlan plan = buildPlan(ctx.local, ctx.remote, ctx.state->entries(), job.direction, md5);
    if (m_cancel.load())
        return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));

    // Never trash a Drive folder that still holds items we do not sync
    // (Google documents etc.): trashing the folder would take them along.
    for (int i = plan.actions.size() - 1; i >= 0; --i) {
        const SyncAction& a = plan.actions.at(i);
        if (a.type != ActionType::DeleteRemote || !a.isDir)
            continue;
        for (const QString& u : qAsConst(ctx.unsyncedRemote)) {
            if (isUnder(u, a.path)) {
                plan.skipped.append(
                    QStringLiteral("%1: Drive folder kept because it contains items that are not synced").arg(a.path));
                plan.actions.remove(i);
                break;
            }
        }
    }

    if (job.interactive) {
        const QVector<SyncAction> conflicts = plan.conflicts();
        if (!conflicts.isEmpty()) {
            QVector<ConflictInfo> infos;
            for (const SyncAction& c : conflicts)
                infos.append({ c.path, c.conflict });
            Q_EMIT conflictsFound(job.folderId, infos);
            if (!waitForAnswer(m_haveChoices))
                return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));
            QHash<QString, ConflictChoice> choices;
            {
                QMutexLocker lock(&m_mutex);
                choices = m_choices;
            }
            const QString suffix
                = QStringLiteral(" (conflict copy %1)").arg(QDate::currentDate().toString(Qt::ISODate));
            plan = applyConflictChoices(plan, choices, ctx.local, ctx.remote, suffix);
        }
        if (plan.hasDeletions()) {
            Q_EMIT deletionsPending(job.folderId, plan.localDeletions(), plan.remoteDeletions());
            if (!waitForAnswer(m_haveDecision))
                return fail(SyncResult::Cancelled, QStringLiteral("Cancelled"));
            bool proceed = false;
            {
                QMutexLocker lock(&m_mutex);
                proceed = m_proceedWithDeletions;
            }
            if (!proceed)
                return fail(SyncResult::Cancelled, QStringLiteral("Sync cancelled: deletions were not confirmed"));
        }
    } else {
        plan = withoutDeletionsAndConflicts(plan, &result.pendingDeletions, &result.pendingConflicts);
    }
    result.skipped += plan.skipped;

    ctx.bytesTotal = plan.transferBytes();
    ctx.filesTotal = plan.transferCount();
    emitProgress(ctx, 0, QString(), true);

    bool aborted = false;
    for (const SyncAction& action : qAsConst(plan.actions)) {
        if (m_cancel.load()) {
            result.status = SyncResult::Cancelled;
            result.errorMessage = QStringLiteral("Cancelled");
            aborted = true;
            break;
        }
        if (!execute(ctx, action)) {
            aborted = true;
            break;
        }
    }

    QString saveError;
    if (!ctx.state->save(&saveError)) {
        result.errors.append(QStringLiteral("Cannot save the sync state: %1").arg(saveError));
    }
    if (!aborted) {
        if (!result.errors.isEmpty()) {
            result.status = SyncResult::Failed;
            result.errorMessage = QStringLiteral("%1 item(s) could not be synced").arg(result.errors.size());
        }
        SyncProgress done;
        done.folderId = job.folderId;
        done.phase = QStringLiteral("done");
        done.bytesTotal = ctx.bytesTotal;
        done.bytesDone = ctx.bytesTotal;
        done.filesTotal = ctx.filesTotal;
        done.filesDone = ctx.filesDone;
        Q_EMIT progress(done);
    }
    return result;
}

/// Executes one action. Returns false when the whole run must stop
/// (cancelled or authorization lost); item failures are only recorded.
bool SyncEngine::execute(Context& ctx, const SyncAction& a)
{
    SyncResult& result = ctx.result;
    StateMap& state = ctx.state->entries();
    const QString abs = ctx.abs(a.path);

    const auto handleError = [&](const DriveError& e) {
        if (e.kind == DriveError::Cancelled) {
            result.status = SyncResult::Cancelled;
            result.errorMessage = QStringLiteral("Cancelled");
            return false;
        }
        if (e.kind == DriveError::Auth) {
            result.status = SyncResult::AuthRequired;
            result.errorMessage = e.message;
            return false;
        }
        result.errors.append(QStringLiteral("%1: %2").arg(a.path, e.message));
        return true;
    };
    const auto transferDone = [&]() {
        ctx.bytesBase += a.bytes;
        ++ctx.filesDone;
        emitProgress(ctx, 0, a.path);
    };

    switch (a.type) {
    case ActionType::RenameLocal: {
        if (!QFile::rename(abs, ctx.abs(a.newPath)))
            result.errors.append(QStringLiteral("%1: cannot create the conflict copy").arg(a.path));
        return true;
    }
    case ActionType::MkdirLocal: {
        if (!QDir().mkpath(abs)) {
            result.errors.append(QStringLiteral("%1: cannot create the folder").arg(a.path));
            return true;
        }
        StateEntry e;
        e.isDir = true;
        e.driveId = ctx.remote.value(a.path).id;
        state.insert(a.path, e);
        return true;
    }
    case ActionType::MkdirRemote: {
        DriveError error;
        if (ensureRemoteDir(ctx, a.path, &error).isEmpty())
            return handleError(error);
        StateEntry e;
        e.isDir = true;
        e.driveId = ctx.remoteDirIds.value(a.path);
        state.insert(a.path, e);
        return true;
    }
    case ActionType::Adopt: {
        const RemoteItem r = ctx.remote.value(a.path);
        StateEntry e;
        e.isDir = a.isDir;
        e.driveId = r.id;
        if (!a.isDir) {
            const QFileInfo info(abs);
            e.size = info.size();
            e.md5 = r.md5;
            e.localMtimeMs = info.lastModified().toMSecsSinceEpoch();
        }
        state.insert(a.path, e);
        return true;
    }
    case ActionType::Forget:
        state.remove(a.path);
        return true;
    case ActionType::Conflict:
        return true; // unresolved: nothing to do
    case ActionType::Download: {
        const RemoteItem r = ctx.remote.value(a.path);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        const QString tmp = abs + partialSuffix();
        emitProgress(ctx, 0, a.path);
        const DriveError e = ctx.client->download(r.id, tmp,
            [&](qint64 done, qint64) { emitProgress(ctx, qMin(done, a.bytes), a.path); });
        if (e) {
            QFile::remove(tmp);
            transferDone();
            return handleError(e);
        }
        if (fileMd5(tmp) != r.md5) {
            QFile::remove(tmp);
            result.errors.append(QStringLiteral("%1: downloaded content did not match the Drive checksum").arg(a.path));
            transferDone();
            return true;
        }
        if (QFileInfo::exists(abs) && !QFile::remove(abs)) {
            QFile::remove(tmp);
            result.errors.append(QStringLiteral("%1: cannot replace the local file").arg(a.path));
            transferDone();
            return true;
        }
        if (!QFile::rename(tmp, abs)) {
            QFile::remove(tmp);
            result.errors.append(QStringLiteral("%1: cannot write the local file").arg(a.path));
            transferDone();
            return true;
        }
        if (r.modifiedMs > 0) {
            QFile f(abs);
            if (f.open(QIODevice::ReadWrite))
                f.setFileTime(QDateTime::fromMSecsSinceEpoch(r.modifiedMs), QFileDevice::FileModificationTime);
        }
        const QFileInfo info(abs);
        StateEntry s;
        s.driveId = r.id;
        s.size = info.size();
        s.md5 = r.md5;
        s.localMtimeMs = info.lastModified().toMSecsSinceEpoch();
        state.insert(a.path, s);
        ++result.downloaded;
        result.bytesTransferred += info.size();
        transferDone();
        return true;
    }
    case ActionType::Upload: {
        const QFileInfo before(abs);
        if (!before.isFile()) {
            result.errors.append(QStringLiteral("%1: local file disappeared").arg(a.path));
            transferDone();
            return true;
        }
        DriveError dirError;
        const QString parentId = ensureRemoteDir(ctx, parentPath(a.path), &dirError);
        if (parentId.isEmpty()) {
            transferDone();
            return handleError(dirError);
        }
        const auto rit = ctx.remote.constFind(a.path);
        const QString existingId = rit != ctx.remote.cend() && !rit->isDir ? rit->id : QString();
        emitProgress(ctx, 0, a.path);
        const auto uploaded = ctx.client->upload(abs, baseName(a.path), parentId, existingId,
            before.lastModified().toMSecsSinceEpoch(),
            [&](qint64 done, qint64) { emitProgress(ctx, qMin(done, a.bytes), a.path); });
        if (!uploaded.ok()) {
            transferDone();
            return handleError(uploaded.error);
        }
        StateEntry s;
        s.driveId = uploaded.value.id;
        s.size = before.size();
        s.md5 = uploaded.value.md5;
        s.localMtimeMs = before.lastModified().toMSecsSinceEpoch();
        state.insert(a.path, s);
        ++result.uploaded;
        result.bytesTransferred += before.size();
        transferDone();
        return true;
    }
    case ActionType::DeleteLocal: {
        bool ok = true;
        if (a.isDir) {
            // only removes empty directories: anything unsynced inside is kept
            if (QFileInfo::exists(abs) && !QDir().rmdir(abs)) {
                ok = false;
                result.skipped.append(QStringLiteral("%1: local folder kept because it is not empty").arg(a.path));
            }
        } else if (QFileInfo::exists(abs)) {
            ok = QFile::moveToTrash(abs) || QFile::remove(abs);
            if (!ok)
                result.errors.append(QStringLiteral("%1: cannot delete the local file").arg(a.path));
        }
        if (ok) {
            state.remove(a.path);
            ++result.deletedLocal;
        }
        return true;
    }
    case ActionType::DeleteRemote: {
        const QString id = ctx.remote.value(a.path).id;
        if (!id.isEmpty()) {
            const DriveError e = ctx.client->trash(id);
            if (e && e.kind != DriveError::NotFound)
                return handleError(e);
        }
        state.remove(a.path);
        ++result.deletedRemote;
        return true;
    }
    }
    return true;
}

} // namespace gds
