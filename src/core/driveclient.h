#pragma once

#include "endpoints.h"
#include "http.h"

#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

class QNetworkAccessManager;

namespace gds {

class TokenSource;

inline QString driveFolderMimeType()
{
    return QStringLiteral("application/vnd.google-apps.folder");
}

struct DriveItem {
    QString id;
    QString name;
    QString mimeType;
    QString md5; ///< empty for folders and Google-native documents
    qint64 size = 0;
    qint64 modifiedMs = 0; ///< modifiedTime as ms since epoch (UTC)

    bool isFolder() const { return mimeType == driveFolderMimeType(); }
    /// Google Docs/Sheets/Slides/shortcuts...: no binary content to sync.
    bool isGoogleNative() const
    {
        return !isFolder() && mimeType.startsWith(QLatin1String("application/vnd.google-apps."));
    }
};

struct DriveError {
    enum Kind { None, Cancelled, Auth, Network, NotFound, Api, LocalIo };
    Kind kind = None;
    int httpStatus = 0;
    QString reason; ///< Drive error reason, e.g. "rateLimitExceeded"
    QString message;

    bool ok() const { return kind == None; }
    explicit operator bool() const { return !ok(); }
};

template <typename T> struct DriveResult {
    T value {};
    DriveError error;
    bool ok() const { return error.ok(); }
};

struct AccountInfo {
    QString email;
    QString displayName;
};

/// Blocking client for the Google Drive REST API v3.
/// Create and use it inside a worker thread; never in the GUI thread.
class DriveClient {
public:
    DriveClient(GoogleEndpoints endpoints, std::shared_ptr<TokenSource> tokens,
        const std::atomic_bool* cancel = nullptr);
    ~DriveClient();
    DriveClient(const DriveClient&) = delete;
    DriveClient& operator=(const DriveClient&) = delete;

    /// about.get: the signed-in user.
    DriveResult<AccountInfo> about();

    /// files.list of the non-trashed children of a folder ("root" for My Drive).
    DriveResult<QVector<DriveItem>> listChildren(const QString& folderId, bool foldersOnly = false);

    /// files.get with alt=media, streamed into \p destPath (overwritten).
    DriveError download(const QString& fileId, const QString& destPath, const HttpProgress& progress = {});

    /// Resumable upload. Creates a new file in \p parentId when \p existingId
    /// is empty, otherwise replaces the content of \p existingId.
    DriveResult<DriveItem> upload(const QString& localPath, const QString& name, const QString& parentId,
        const QString& existingId, qint64 modifiedMs, const HttpProgress& progress = {});

    DriveResult<DriveItem> createFolder(const QString& name, const QString& parentId);

    /// Moves a file or folder to the Drive trash (files.update trashed=true).
    DriveError trash(const QString& fileId);

    /// Maximum retries for transient failures (429/5xx/rate limits).
    void setMaxRetries(int n) { m_maxRetries = n; }
    void setRetryBaseDelayMs(int ms) { m_retryBaseMs = ms; }

    static QString escapeQueryValue(const QString& value);

private:
    HttpResponse send(const HttpRequest& request, const HttpProgress& progress = {}, bool retryable = true);
    static DriveError toError(const HttpResponse& response);
    static DriveItem parseItem(const QJsonObject& obj);
    bool sleepCancellable(int ms) const;
    bool cancelled() const { return m_cancel && m_cancel->load(); }

    GoogleEndpoints m_endpoints;
    std::shared_ptr<TokenSource> m_tokens;
    const std::atomic_bool* m_cancel;
    std::unique_ptr<QNetworkAccessManager> m_nam;
    int m_maxRetries = 5;
    int m_retryBaseMs = 500;
    QString m_lastTokenError;
};

} // namespace gds
