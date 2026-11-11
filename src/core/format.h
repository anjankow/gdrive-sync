#pragma once

#include "synctypes.h"

namespace gds {

/// "1.5 MiB", "12 B", ...
QString formatBytes(qint64 bytes);

/// "3.0 MiB of 10.0 MiB synced, 7.0 MiB remaining (2 of 5 files)"
QString formatProgress(const SyncProgress& progress);

/// 0..1000 for a progress bar.
int progressPermille(const SyncProgress& progress);

QString formatResult(const SyncResult& result);

} // namespace gds
