#include "format.h"

namespace gds {

QString formatBytes(qint64 bytes)
{
    static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(qMax<qint64>(0, bytes));
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

QString formatProgress(const SyncProgress& p)
{
    if (p.phase == QLatin1String("scan"))
        return QStringLiteral("Comparing local folder and Google Drive…");
    return QStringLiteral("%1 of %2 synced, %3 remaining (%4 of %5 files)")
        .arg(formatBytes(p.bytesDone), formatBytes(p.bytesTotal), formatBytes(p.bytesRemaining()))
        .arg(p.filesDone)
        .arg(p.filesTotal);
}

int progressPermille(const SyncProgress& p)
{
    if (p.phase == QLatin1String("done"))
        return 1000;
    if (p.bytesTotal <= 0)
        return p.filesTotal > 0 ? int(1000LL * p.filesDone / p.filesTotal) : 0;
    return int(qBound<qint64>(0, 1000 * p.bytesDone / p.bytesTotal, 1000));
}

QString formatResult(const SyncResult& r)
{
    QString text;
    switch (r.status) {
    case SyncResult::Success:
        text = QStringLiteral("Synced: %1 downloaded, %2 uploaded, %3 deleted locally, %4 deleted on Drive (%5)")
                   .arg(r.downloaded)
                   .arg(r.uploaded)
                   .arg(r.deletedLocal)
                   .arg(r.deletedRemote)
                   .arg(formatBytes(r.bytesTransferred));
        break;
    case SyncResult::Cancelled:
        text = r.errorMessage.isEmpty() ? QStringLiteral("Cancelled") : r.errorMessage;
        break;
    case SyncResult::AuthRequired:
        text = QStringLiteral("Sign-in required: %1").arg(r.errorMessage);
        break;
    case SyncResult::Failed:
        text = QStringLiteral("Failed: %1").arg(r.errorMessage);
        break;
    }
    if (r.needsAttention()) {
        text += QStringLiteral(" — %1 deletion(s) and %2 conflict(s) pending; run a manual sync to review them")
                    .arg(r.pendingDeletions)
                    .arg(r.pendingConflicts);
    }
    return text;
}

} // namespace gds
