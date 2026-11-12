#include "driveclient.h"

#include "tokensource.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRandomGenerator>
#include <QThread>
#include <QUrlQuery>

namespace gds {

namespace {
    const QString kItemFields = QStringLiteral("id,name,mimeType,md5Checksum,size,modifiedTime");

    QString toRfc3339(qint64 ms)
    {
        return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC).toString(Qt::ISODateWithMs);
    }

    QUrl withQuery(const QString& base, const QList<QPair<QString, QString>>& items)
    {
        QUrl url(base);
        QUrlQuery q;
        for (const auto& i : items)
            q.addQueryItem(i.first, QString::fromUtf8(QUrl::toPercentEncoding(i.second)));
        url.setQuery(q.query(QUrl::FullyEncoded), QUrl::StrictMode);
        return url;
    }
}

DriveClient::DriveClient(
    GoogleEndpoints endpoints, std::shared_ptr<TokenSource> tokens, const std::atomic_bool* cancel)
    : m_endpoints(std::move(endpoints))
    , m_tokens(std::move(tokens))
    , m_cancel(cancel)
    , m_nam(std::make_unique<QNetworkAccessManager>())
{
}

DriveClient::~DriveClient() = default;

QString DriveClient::escapeQueryValue(const QString& value)
{
    QString out = value;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('\''), QLatin1String("\\'"));
    return out;
}

bool DriveClient::sleepCancellable(int ms) const
{
    int waited = 0;
    while (waited < ms) {
        if (cancelled())
            return false;
        const int step = qMin(50, ms - waited);
        QThread::msleep(step);
        waited += step;
    }
    return !cancelled();
}

DriveItem DriveClient::parseItem(const QJsonObject& obj)
{
    DriveItem item;
    item.id = obj.value(QStringLiteral("id")).toString();
    item.name = obj.value(QStringLiteral("name")).toString();
    item.mimeType = obj.value(QStringLiteral("mimeType")).toString();
    item.md5 = obj.value(QStringLiteral("md5Checksum")).toString();
    // int64 values are transported as JSON strings
    item.size = obj.value(QStringLiteral("size")).toVariant().toLongLong();
    const QDateTime modified
        = QDateTime::fromString(obj.value(QStringLiteral("modifiedTime")).toString(), Qt::ISODateWithMs);
    item.modifiedMs = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;
    return item;
}

DriveError DriveClient::toError(const HttpResponse& response)
{
    DriveError e;
    if (response.cancelled) {
        e.kind = DriveError::Cancelled;
        e.message = QStringLiteral("Cancelled");
        return e;
    }
    if (response.isSuccess())
        return e;
    e.httpStatus = response.status;
    if (response.status == 0) {
        e.kind = DriveError::Network;
        e.message = response.errorString.isEmpty() ? QStringLiteral("Network error") : response.errorString;
        return e;
    }
    const QJsonObject err
        = QJsonDocument::fromJson(response.body).object().value(QStringLiteral("error")).toObject();
    e.message = err.value(QStringLiteral("message")).toString();
    const QJsonArray errors = err.value(QStringLiteral("errors")).toArray();
    if (!errors.isEmpty())
        e.reason = errors.first().toObject().value(QStringLiteral("reason")).toString();
    if (e.message.isEmpty())
        e.message = QStringLiteral("HTTP %1").arg(response.status);
    if (response.status == 401)
        e.kind = DriveError::Auth;
    else if (response.status == 404)
        e.kind = DriveError::NotFound;
    else
        e.kind = DriveError::Api;
    return e;
}

/// Sends an authorized request. Handles token refresh on 401 and retries
/// transient failures with exponential backoff as recommended in
/// https://developers.google.com/workspace/drive/api/guides/handle-errors
HttpResponse DriveClient::send(const HttpRequest& request, const HttpProgress& progress, bool retryable)
{
    bool refreshed = false;
    for (int attempt = 0;; ++attempt) {
        QString tokenError;
        const QString token = m_tokens->accessToken(&tokenError);
        if (token.isEmpty()) {
            HttpResponse r;
            r.status = 401;
            r.body = QJsonDocument(QJsonObject {{QStringLiteral("error"),
                                       QJsonObject {{QStringLiteral("message"), tokenError}}}})
                         .toJson();
            return r;
        }
        HttpRequest req = request;
        req.headers.append(HttpHeader("Authorization", "Bearer " + token.toUtf8()));
        if (req.bodyDevice)
            req.bodyDevice->seek(0);
        if (req.sink) {
            req.sink->seek(0);
            if (auto* f = qobject_cast<QFile*>(req.sink))
                f->resize(0);
        }
        HttpResponse resp = httpSend(*m_nam, req, m_cancel, progress);
        if (resp.cancelled || resp.isSuccess() || resp.status == 308)
            return resp;

        if (resp.status == 401 && !refreshed) {
            refreshed = true;
            if (!m_tokens->accessToken(&tokenError, true).isEmpty())
                continue;
            return resp;
        }

        bool transient = resp.status == 0 || resp.status == 429 || resp.status >= 500;
        if (resp.status == 403) {
            const QString reason = toError(resp).reason;
            transient = reason == QLatin1String("rateLimitExceeded")
                || reason == QLatin1String("userRateLimitExceeded");
        }
        const bool maybeProcessed = resp.status == 0 || resp.status >= 500;
        if (!transient || attempt >= m_maxRetries || (!retryable && maybeProcessed))
            return resp;
        const int delay = m_retryBaseMs * (1 << attempt)
            + int(QRandomGenerator::global()->bounded(m_retryBaseMs / 2 + 1));
        if (!sleepCancellable(delay)) {
            resp.cancelled = true;
            return resp;
        }
    }
}

DriveResult<AccountInfo> DriveClient::about()
{
    HttpRequest req;
    req.url = withQuery(m_endpoints.apiBase + QStringLiteral("/about"),
        {{QStringLiteral("fields"), QStringLiteral("user(displayName,emailAddress,photoLink)")}});
    const HttpResponse resp = send(req);
    DriveResult<AccountInfo> result;
    result.error = toError(resp);
    if (result.ok()) {
        const QJsonObject user
            = QJsonDocument::fromJson(resp.body).object().value(QStringLiteral("user")).toObject();
        result.value.email = user.value(QStringLiteral("emailAddress")).toString();
        result.value.displayName = user.value(QStringLiteral("displayName")).toString();
        result.value.photoLink = user.value(QStringLiteral("photoLink")).toString();
    }
    return result;
}

DriveResult<QVector<DriveItem>> DriveClient::listChildren(const QString& folderId, bool foldersOnly)
{
    DriveResult<QVector<DriveItem>> result;
    QString q = QStringLiteral("'%1' in parents and trashed = false").arg(escapeQueryValue(folderId));
    if (foldersOnly)
        q += QStringLiteral(" and mimeType = '%1'").arg(driveFolderMimeType());
    QString pageToken;
    do {
        QList<QPair<QString, QString>> items {
            {QStringLiteral("q"), q},
            {QStringLiteral("fields"), QStringLiteral("nextPageToken,files(%1)").arg(kItemFields)},
            {QStringLiteral("pageSize"), QStringLiteral("1000")},
            {QStringLiteral("spaces"), QStringLiteral("drive")},
            {QStringLiteral("orderBy"), QStringLiteral("name")},
        };
        if (!pageToken.isEmpty())
            items.append({QStringLiteral("pageToken"), pageToken});
        HttpRequest req;
        req.url = withQuery(m_endpoints.apiBase + QStringLiteral("/files"), items);
        const HttpResponse resp = send(req);
        result.error = toError(resp);
        if (!result.ok())
            return result;
        const QJsonObject obj = QJsonDocument::fromJson(resp.body).object();
        const QJsonArray files = obj.value(QStringLiteral("files")).toArray();
        for (const QJsonValue& f : files)
            result.value.append(parseItem(f.toObject()));
        pageToken = obj.value(QStringLiteral("nextPageToken")).toString();
    } while (!pageToken.isEmpty());
    return result;
}

DriveError DriveClient::download(const QString& fileId, const QString& destPath, const HttpProgress& progress)
{
    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        DriveError e;
        e.kind = DriveError::LocalIo;
        e.message = QStringLiteral("Cannot write %1: %2").arg(destPath, file.errorString());
        return e;
    }
    HttpRequest req;
    req.url = withQuery(m_endpoints.apiBase + QStringLiteral("/files/") + fileId,
        {{QStringLiteral("alt"), QStringLiteral("media")}});
    req.sink = &file;
    req.followRedirects = true;
    const HttpResponse resp = send(req, progress);
    DriveError error = toError(resp);
    if (error.ok() && !file.flush()) {
        error.kind = DriveError::LocalIo;
        error.message = QStringLiteral("Cannot write %1: %2").arg(destPath, file.errorString());
    }
    file.close();
    if (!error.ok())
        QFile::remove(destPath);
    return error;
}

DriveResult<DriveItem> DriveClient::upload(const QString& localPath, const QString& name,
    const QString& parentId, const QString& existingId, qint64 modifiedMs, const HttpProgress& progress)
{
    DriveResult<DriveItem> result;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error.kind = DriveError::LocalIo;
        result.error.message = QStringLiteral("Cannot read %1: %2").arg(localPath, file.errorString());
        return result;
    }
    const qint64 size = file.size();

    // 1. Start a resumable session (POST to create, PATCH to update).
    QJsonObject meta;
    if (modifiedMs > 0)
        meta.insert(QStringLiteral("modifiedTime"), toRfc3339(modifiedMs));
    QString base = m_endpoints.uploadBase + QStringLiteral("/files");
    HttpRequest init;
    if (existingId.isEmpty()) {
        init.method = "POST";
        meta.insert(QStringLiteral("name"), name);
        meta.insert(QStringLiteral("parents"), QJsonArray {parentId});
    } else {
        init.method = "PATCH";
        base += QLatin1Char('/') + existingId;
    }
    init.url = withQuery(base,
        {{QStringLiteral("uploadType"), QStringLiteral("resumable")},
            {QStringLiteral("fields"), kItemFields}});
    init.body = QJsonDocument(meta).toJson(QJsonDocument::Compact);
    init.headers.append(HttpHeader("Content-Type", "application/json; charset=UTF-8"));
    init.headers.append(HttpHeader("X-Upload-Content-Type", "application/octet-stream"));
    init.headers.append(HttpHeader("X-Upload-Content-Length", QByteArray::number(size)));
    const HttpResponse initResp = send(init);
    result.error = toError(initResp);
    if (!result.ok())
        return result;
    const QUrl session = QUrl::fromEncoded(initResp.header("Location"));
    if (!session.isValid() || session.isEmpty()) {
        result.error.kind = DriveError::Api;
        result.error.message = QStringLiteral("Drive did not return an upload session");
        return result;
    }

    // 2. Send the content; on interruption ask how much arrived and resume.
    qint64 offset = 0;
    for (int attempt = 0;; ++attempt) {
        if (!file.seek(offset)) {
            result.error.kind = DriveError::LocalIo;
            result.error.message = QStringLiteral("Cannot read %1: %2").arg(localPath, file.errorString());
            return result;
        }
        HttpRequest put;
        put.method = "PUT";
        put.url = session;
        put.headers.append(HttpHeader("Content-Type", "application/octet-stream"));
        HttpResponse resp;
        const QString token = m_tokens->accessToken();
        if (!token.isEmpty())
            put.headers.append(HttpHeader("Authorization", "Bearer " + token.toUtf8()));
        if (size == 0) {
            put.headers.append(HttpHeader("Content-Length", "0"));
            resp = httpSend(*m_nam, put, m_cancel);
        } else {
            if (offset > 0) {
                put.headers.append(HttpHeader("Content-Range",
                    "bytes " + QByteArray::number(offset) + '-' + QByteArray::number(size - 1) + '/'
                        + QByteArray::number(size)));
            }
            put.bodyDevice = &file;
            put.bodyDeviceSize = size - offset;
            const qint64 base0 = offset;
            resp = httpSend(*m_nam, put, m_cancel, [&](qint64 done, qint64) {
                if (progress)
                    progress(base0 + done, size);
            });
        }
        if (resp.cancelled) {
            result.error = toError(resp);
            return result;
        }
        if (resp.status == 200 || resp.status == 201) {
            result.value = parseItem(QJsonDocument::fromJson(resp.body).object());
            if (progress)
                progress(size, size);
            return result;
        }
        // 308 on a content request means Drive holds only a part of the file.
        const bool incomplete = resp.status == 308;
        const bool transient = resp.status == 0 || resp.status == 429 || resp.status >= 500;
        if ((!transient && !incomplete) || attempt >= m_maxRetries) {
            result.error = toError(resp);
            if (result.ok()) {
                result.error.kind = DriveError::Api;
                result.error.message
                    = QStringLiteral("The upload could not be completed (HTTP %1)").arg(resp.status);
            }
            return result;
        }
        if (!sleepCancellable(m_retryBaseMs * (1 << attempt))) {
            result.error.kind = DriveError::Cancelled;
            return result;
        }
        HttpResponse st = resp;
        if (!incomplete) {
            // Ask how much arrived: "Content-Range: bytes */size".
            HttpRequest status;
            status.method = "PUT";
            status.url = session;
            status.headers.append(HttpHeader("Content-Range", "bytes */" + QByteArray::number(size)));
            status.headers.append(HttpHeader("Content-Length", "0"));
            st = httpSend(*m_nam, status, m_cancel);
        }
        if (st.status == 200 || st.status == 201) {
            result.value = parseItem(QJsonDocument::fromJson(st.body).object());
            return result;
        }
        if (st.status == 308) {
            // "Range: bytes=0-42" -> resume at 43; no header -> nothing stored yet
            const QByteArray range = st.header("Range");
            const int dash = range.lastIndexOf('-');
            offset = dash >= 0 ? range.mid(dash + 1).toLongLong() + 1 : 0;
        } else if (st.status == 404) {
            result.error = toError(st); // session expired: caller has to start over
            return result;
        }
        // otherwise keep the previous offset and try again
    }
}

DriveResult<DriveItem> DriveClient::createFolder(const QString& name, const QString& parentId)
{
    HttpRequest req;
    req.method = "POST";
    req.url = withQuery(
        m_endpoints.apiBase + QStringLiteral("/files"), {{QStringLiteral("fields"), kItemFields}});
    req.headers.append(HttpHeader("Content-Type", "application/json; charset=UTF-8"));
    req.body = QJsonDocument(QJsonObject {
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("mimeType"), driveFolderMimeType()},
                                 {QStringLiteral("parents"), QJsonArray {parentId}},
                             })
                   .toJson(QJsonDocument::Compact);
    // Not retried blindly: a retry after a lost response could create a duplicate folder.
    const HttpResponse resp = send(req, {}, false);
    DriveResult<DriveItem> result;
    result.error = toError(resp);
    if (result.ok())
        result.value = parseItem(QJsonDocument::fromJson(resp.body).object());
    return result;
}

DriveError DriveClient::trash(const QString& fileId)
{
    HttpRequest req;
    req.method = "PATCH";
    req.url = withQuery(m_endpoints.apiBase + QStringLiteral("/files/") + fileId,
        {{QStringLiteral("fields"), QStringLiteral("id,trashed")}});
    req.headers.append(HttpHeader("Content-Type", "application/json; charset=UTF-8"));
    req.body = QJsonDocument(QJsonObject {{QStringLiteral("trashed"), true}}).toJson(QJsonDocument::Compact);
    return toError(send(req));
}

} // namespace gds
