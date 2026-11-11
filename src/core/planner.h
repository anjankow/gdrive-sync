#pragma once

#include "synctypes.h"

#include <functional>

namespace gds {

/// Returns the MD5 (hex) of the local file at the given relative path.
using Md5Provider = std::function<QString(const QString& relPath)>;

/// Pure three-way comparison of the local tree, the remote tree and the
/// state of the last sync. Produces the list of actions for one sync run.
SyncPlan buildPlan(const LocalTree& local, const RemoteTree& remote, const StateMap& state,
    SyncDirection direction, const Md5Provider& localMd5);

/// Replaces the Conflict actions of \p plan according to the user's choices.
/// Paths without a choice (or with Skip) are dropped from the plan.
SyncPlan applyConflictChoices(const SyncPlan& plan, const QHash<QString, ConflictChoice>& choices,
    const LocalTree& local, const RemoteTree& remote, const QString& conflictSuffix);

/// Removes deletions and conflicts (used by unattended runs).
SyncPlan withoutDeletionsAndConflicts(const SyncPlan& plan, int* droppedDeletions, int* droppedConflicts);

/// Orders actions for execution: renames, directory creation (parents
/// first), transfers, then deletions (children before their directories).
void sortForExecution(SyncPlan& plan);

/// "report.txt" + " (conflict copy 2026-01-31)" -> "report (conflict copy 2026-01-31).txt"
QString conflictCopyPath(const QString& path, const QString& suffix, const LocalTree& local);

} // namespace gds
