#include "planner.h"

#include <QSet>

#include <algorithm>

namespace gds {

QString directionLabel(SyncDirection d)
{
    switch (d) {
    case SyncDirection::TwoWay:
        return QStringLiteral("Two-way");
    case SyncDirection::DriveToLocal:
        return QStringLiteral("Drive to local");
    case SyncDirection::LocalToDrive:
        return QStringLiteral("Local to Drive");
    case SyncDirection::Unset:
        break;
    }
    return QStringLiteral("Not selected");
}

qint64 SyncPlan::transferBytes() const
{
    qint64 total = 0;
    for (const SyncAction& a : actions) {
        if (a.type == ActionType::Download || a.type == ActionType::Upload)
            total += a.bytes;
    }
    return total;
}

int SyncPlan::transferCount() const
{
    return int(std::count_if(actions.cbegin(), actions.cend(), [](const SyncAction& a) {
        return a.type == ActionType::Download || a.type == ActionType::Upload;
    }));
}

QVector<SyncAction> SyncPlan::conflicts() const
{
    QVector<SyncAction> out;
    for (const SyncAction& a : actions) {
        if (a.type == ActionType::Conflict)
            out.append(a);
    }
    return out;
}

QStringList SyncPlan::localDeletions() const
{
    QStringList out;
    for (const SyncAction& a : actions) {
        if (a.type == ActionType::DeleteLocal)
            out.append(a.isDir ? a.path + QLatin1Char('/') : a.path);
    }
    return out;
}

QStringList SyncPlan::remoteDeletions() const
{
    QStringList out;
    for (const SyncAction& a : actions) {
        if (a.type == ActionType::DeleteRemote)
            out.append(a.isDir ? a.path + QLatin1Char('/') : a.path);
    }
    return out;
}

bool SyncPlan::hasDeletions() const
{
    return std::any_of(actions.cbegin(), actions.cend(), [](const SyncAction& a) {
        return a.type == ActionType::DeleteLocal || a.type == ActionType::DeleteRemote;
    });
}

namespace {

    SyncAction make(ActionType type, const QString& path, bool isDir = false, qint64 bytes = 0,
        ConflictKind conflict = ConflictKind::None)
    {
        SyncAction a;
        a.type = type;
        a.path = path;
        a.isDir = isDir;
        a.bytes = bytes;
        a.conflict = conflict;
        return a;
    }

    bool isUnder(const QString& path, const QString& dir)
    {
        return path.size() > dir.size() && path.startsWith(dir) && path.at(dir.size()) == QLatin1Char('/');
    }

    int depth(const QString& path) { return path.count(QLatin1Char('/')); }

    void planFile(SyncPlan& plan, const QString& path, const LocalItem* l, const RemoteItem* r,
        const StateEntry* s, SyncDirection dir, const Md5Provider& localMd5)
    {
        const bool twoWay = dir == SyncDirection::TwoWay;
        const bool d2l = dir == SyncDirection::DriveToLocal;
        const bool l2d = dir == SyncDirection::LocalToDrive;
        const auto add = [&](ActionType t, qint64 bytes = 0, ConflictKind c = ConflictKind::None) {
            plan.actions.append(make(t, path, false, bytes, c));
        };

        if (!l && !r) {
            if (s)
                add(ActionType::Forget);
            return;
        }

        // A file counts as locally changed when size/mtime differ from the
        // recorded state and the content hash confirms it.
        bool localChanged = false;
        if (l && s) {
            if (l->size != s->size) {
                localChanged = true;
            } else if (l->mtimeMs != s->localMtimeMs) {
                localChanged = localMd5(path) != s->md5;
            }
        }
        const bool remoteChanged = r && s && r->md5 != s->md5;

        if (l && !r) {
            if (!s) { // new local file
                if (twoWay || l2d)
                    add(ActionType::Upload, l->size);
                return;
            }
            // was synced before, now missing on Drive
            if (l2d) {
                add(ActionType::Upload, l->size);
            } else if (d2l) {
                add(ActionType::DeleteLocal);
            } else if (localChanged) {
                add(ActionType::Conflict, 0, ConflictKind::LocalModifiedRemoteDeleted);
            } else {
                add(ActionType::DeleteLocal);
            }
            return;
        }

        if (!l && r) {
            if (!s) { // new remote file
                if (twoWay || d2l)
                    add(ActionType::Download, r->size);
                return;
            }
            // was synced before, now missing locally
            if (d2l) {
                add(ActionType::Download, r->size);
            } else if (l2d) {
                add(ActionType::DeleteRemote);
            } else if (remoteChanged) {
                add(ActionType::Conflict, 0, ConflictKind::RemoteModifiedLocalDeleted);
            } else {
                add(ActionType::DeleteRemote);
            }
            return;
        }

        // exists on both sides
        if (!s) {
            if (l->size == r->size && localMd5(path) == r->md5) {
                add(ActionType::Adopt);
            } else if (twoWay) {
                add(ActionType::Conflict, 0, ConflictKind::BothModified);
            } else if (d2l) {
                add(ActionType::Download, r->size);
            } else {
                add(ActionType::Upload, l->size);
            }
            return;
        }

        if (!localChanged && !remoteChanged) {
            // refresh bookkeeping when only mtime or the Drive id changed
            if (l->mtimeMs != s->localMtimeMs || r->id != s->driveId)
                add(ActionType::Adopt);
            return;
        }
        if (localChanged && remoteChanged) {
            if (l->size == r->size && localMd5(path) == r->md5) {
                add(ActionType::Adopt);
            } else if (twoWay) {
                add(ActionType::Conflict, 0, ConflictKind::BothModified);
            } else if (d2l) {
                add(ActionType::Download, r->size);
            } else {
                add(ActionType::Upload, l->size);
            }
            return;
        }
        if (localChanged) {
            if (twoWay || l2d)
                add(ActionType::Upload, l->size);
            return;
        }
        // remote changed only
        if (twoWay || d2l)
            add(ActionType::Download, r->size);
    }

} // namespace

SyncPlan buildPlan(const LocalTree& local, const RemoteTree& remote, const StateMap& state,
    SyncDirection direction, const Md5Provider& localMd5)
{
    SyncPlan plan;
    if (direction == SyncDirection::Unset)
        return plan;

    QSet<QString> pathSet;
    for (auto it = local.cbegin(); it != local.cend(); ++it)
        pathSet.insert(it.key());
    for (auto it = remote.cbegin(); it != remote.cend(); ++it)
        pathSet.insert(it.key());
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        pathSet.insert(it.key());
    QStringList paths = pathSet.values();
    std::sort(paths.begin(), paths.end());

    const bool twoWay = direction == SyncDirection::TwoWay;
    const bool d2l = direction == SyncDirection::DriveToLocal;
    const bool l2d = direction == SyncDirection::LocalToDrive;

    // Paths blocked because a file on one side is a folder on the other.
    QStringList blocked;
    const auto isBlocked = [&blocked](const QString& p) {
        return std::any_of(blocked.cbegin(), blocked.cend(),
            [&p](const QString& b) { return p == b || isUnder(p, b); });
    };

    QStringList dirsGoneRemotely; // in state, still local, missing on Drive
    QStringList dirsGoneLocally; // in state, still on Drive, missing locally

    for (const QString& path : qAsConst(paths)) {
        if (isBlocked(path))
            continue;
        const auto lit = local.constFind(path);
        const auto rit = remote.constFind(path);
        const auto sit = state.constFind(path);
        const LocalItem* l = lit != local.cend() ? &lit.value() : nullptr;
        const RemoteItem* r = rit != remote.cend() ? &rit.value() : nullptr;
        const StateEntry* s = sit != state.cend() ? &sit.value() : nullptr;

        if (l && r && l->isDir != r->isDir) {
            blocked.append(path);
            plan.skipped.append(
                QStringLiteral("%1: is a folder on one side and a file on the other; not synced").arg(path));
            continue;
        }
        const bool isDir = l ? l->isDir : (r ? r->isDir : s->isDir);
        if (s && s->isDir != isDir)
            s = nullptr; // type changed since the last sync: treat as new

        if (!isDir) {
            planFile(plan, path, l, r, s, direction, localMd5);
            continue;
        }

        // directories
        if (l && r) {
            if (!s || s->driveId != r->id)
                plan.actions.append(make(ActionType::Adopt, path, true));
        } else if (l) {
            if (!s) {
                if (twoWay || l2d)
                    plan.actions.append(make(ActionType::MkdirRemote, path, true));
            } else if (l2d) {
                plan.actions.append(make(ActionType::MkdirRemote, path, true));
            } else {
                dirsGoneRemotely.append(path);
            }
        } else if (r) {
            if (!s) {
                if (twoWay || d2l)
                    plan.actions.append(make(ActionType::MkdirLocal, path, true));
            } else if (d2l) {
                plan.actions.append(make(ActionType::MkdirLocal, path, true));
            } else {
                dirsGoneLocally.append(path);
            }
        } else if (s) {
            plan.actions.append(make(ActionType::Forget, path, true));
        }
    }

    // A directory deleted on the other side is deleted here only when
    // everything inside it is going away as well; otherwise it is kept
    // (and re-created on the other side in two-way mode).
    const auto resolveGoneDirs = [&](QStringList dirs, bool goneRemotely) {
        const ActionType deleteType = goneRemotely ? ActionType::DeleteLocal : ActionType::DeleteRemote;
        const ActionType mkdirType = goneRemotely ? ActionType::MkdirRemote : ActionType::MkdirLocal;
        QSet<QString> deleting;
        for (const SyncAction& a : qAsConst(plan.actions)) {
            if (a.type == deleteType)
                deleting.insert(a.path);
        }
        // deepest first so that parents see the decision for their children
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
            const int da = depth(a), db = depth(b);
            return da != db ? da > db : a < b;
        });
        for (const QString& dir : qAsConst(dirs)) {
            bool allGoing = true;
            const auto check = [&](const QString& p) {
                if (isUnder(p, dir) && !deleting.contains(p))
                    allGoing = false;
            };
            if (goneRemotely) {
                for (auto it = local.lowerBound(dir); it != local.cend() && allGoing; ++it) {
                    if (it.key() != dir && !isUnder(it.key(), dir) && it.key() > dir + QLatin1Char('0'))
                        break;
                    check(it.key());
                }
            } else {
                for (auto it = remote.lowerBound(dir); it != remote.cend() && allGoing; ++it) {
                    if (it.key() != dir && !isUnder(it.key(), dir) && it.key() > dir + QLatin1Char('0'))
                        break;
                    check(it.key());
                }
            }
            if (allGoing) {
                deleting.insert(dir);
                plan.actions.append(make(deleteType, dir, true));
            } else if (twoWay) {
                plan.actions.append(make(mkdirType, dir, true));
            }
        }
    };
    resolveGoneDirs(dirsGoneRemotely, true);
    resolveGoneDirs(dirsGoneLocally, false);

    sortForExecution(plan);
    return plan;
}

QString conflictCopyPath(const QString& path, const QString& suffix, const LocalTree& local)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash >= 0 ? path.left(slash + 1) : QString();
    const QString name = path.mid(slash + 1);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString ext = dot > 0 ? name.mid(dot) : QString();
    QString candidate = dir + stem + suffix + ext;
    for (int n = 2; local.contains(candidate); ++n)
        candidate = dir + stem + suffix + QStringLiteral(" %1").arg(n) + ext;
    return candidate;
}

SyncPlan applyConflictChoices(const SyncPlan& plan, const QHash<QString, ConflictChoice>& choices,
    const LocalTree& local, const RemoteTree& remote, const QString& conflictSuffix)
{
    SyncPlan out;
    out.skipped = plan.skipped;
    for (const SyncAction& a : plan.actions) {
        if (a.type != ActionType::Conflict) {
            out.actions.append(a);
            continue;
        }
        const ConflictChoice choice = choices.value(a.path, ConflictChoice::Skip);
        const qint64 localSize = local.value(a.path).size;
        const qint64 remoteSize = remote.value(a.path).size;
        if (choice == ConflictChoice::Skip) {
            out.skipped.append(QStringLiteral("%1: conflict left unresolved").arg(a.path));
            continue;
        }
        switch (a.conflict) {
        case ConflictKind::BothModified:
            if (choice == ConflictChoice::KeepLocal) {
                out.actions.append(make(ActionType::Upload, a.path, false, localSize));
            } else if (choice == ConflictChoice::KeepRemote) {
                out.actions.append(make(ActionType::Download, a.path, false, remoteSize));
            } else {
                SyncAction rename = make(ActionType::RenameLocal, a.path);
                rename.newPath = conflictCopyPath(a.path, conflictSuffix, local);
                out.actions.append(rename);
                out.actions.append(make(ActionType::Download, a.path, false, remoteSize));
                out.actions.append(make(ActionType::Upload, rename.newPath, false, localSize));
            }
            break;
        case ConflictKind::LocalModifiedRemoteDeleted:
            if (choice == ConflictChoice::KeepRemote)
                out.actions.append(make(ActionType::DeleteLocal, a.path));
            else
                out.actions.append(make(ActionType::Upload, a.path, false, localSize));
            break;
        case ConflictKind::RemoteModifiedLocalDeleted:
            if (choice == ConflictChoice::KeepLocal)
                out.actions.append(make(ActionType::DeleteRemote, a.path));
            else
                out.actions.append(make(ActionType::Download, a.path, false, remoteSize));
            break;
        case ConflictKind::None:
            break;
        }
    }
    sortForExecution(out);
    return out;
}

SyncPlan withoutDeletionsAndConflicts(const SyncPlan& plan, int* droppedDeletions, int* droppedConflicts)
{
    SyncPlan out;
    out.skipped = plan.skipped;
    int deletions = 0;
    int conflicts = 0;
    for (const SyncAction& a : plan.actions) {
        if (a.type == ActionType::DeleteLocal || a.type == ActionType::DeleteRemote) {
            ++deletions;
        } else if (a.type == ActionType::Conflict) {
            ++conflicts;
        } else {
            out.actions.append(a);
        }
    }
    if (droppedDeletions)
        *droppedDeletions = deletions;
    if (droppedConflicts)
        *droppedConflicts = conflicts;
    return out;
}

void sortForExecution(SyncPlan& plan)
{
    const auto rank = [](const SyncAction& a) {
        switch (a.type) {
        case ActionType::RenameLocal:
            return 0;
        case ActionType::MkdirLocal:
        case ActionType::MkdirRemote:
            return 1;
        case ActionType::Adopt:
        case ActionType::Forget:
            return 2;
        case ActionType::Download:
        case ActionType::Upload:
            return 3;
        case ActionType::Conflict:
            return 4;
        case ActionType::DeleteLocal:
        case ActionType::DeleteRemote:
            return 5;
        }
        return 6;
    };
    std::stable_sort(plan.actions.begin(), plan.actions.end(), [&](const SyncAction& a, const SyncAction& b) {
        const int ra = rank(a), rb = rank(b);
        if (ra != rb)
            return ra < rb;
        if (ra == 5) { // deletions: deepest paths first, files before their directory
            const int da = depth(a.path), db = depth(b.path);
            if (da != db)
                return da > db;
            if (a.isDir != b.isDir)
                return !a.isDir;
        }
        return a.path < b.path;
    });
}

} // namespace gds
