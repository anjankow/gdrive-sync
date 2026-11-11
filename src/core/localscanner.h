#pragma once

#include "synctypes.h"

#include <atomic>

namespace gds {

inline QString partialSuffix() { return QStringLiteral(".gdsync-part"); }

/// Recursively lists regular files and directories below \p root.
/// Symlinks, special files and in-progress downloads are ignored
/// (reported through \p skipped).
LocalTree scanLocalTree(const QString& root, QStringList* skipped = nullptr,
    const std::atomic_bool* cancel = nullptr);

/// MD5 of a file as lower-case hex; empty on error or cancellation.
QString fileMd5(const QString& path, const std::atomic_bool* cancel = nullptr);

} // namespace gds
