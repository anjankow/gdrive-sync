#include "mockgoogleserver.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace {
const QString kFolderMime = QStringLiteral("application/vnd.google-apps.folder");

QMap<QString, QString> parseForm(const QByteArray& body)
{
    QMap<QString, QString> out;
    const QUrlQuery q(QString::fromUtf8(body));
    const auto items = q.queryItems(QUrl::FullyDecoded);
    for (const auto& i : items)
        out.insert(i.first, QString(i.second).replace(QLatin1Char('+'), QLatin1Char(' ')));
    return out;
}

QByteArray statusText(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 302:
        return "Found";
    case 308:
        return "Resume Incomplete";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 429:
        return "Too Many Requests";
    default:
        return status >= 500 ? "Server Error" : "Status";
    }
}
}

MockGoogleServer::MockGoogleServer(QObject* parent)
    : QObject(parent)
{
    // The server object lives in its own thread so that it keeps answering
    // while the test thread is blocked or busy.
    m_server = new QTcpServer;
    m_server->listen(QHostAddress::LocalHost, 0);
    m_port = m_server->serverPort();
    m_server->moveToThread(&m_thread);
    connect(m_server, &QTcpServer::newConnection, m_server, [this]() { onNewConnection(); });
    m_thread.start();
}

MockGoogleServer::~MockGoogleServer()
{
    QMetaObject::invokeMethod(
        m_server,
        [this]() {
            m_server->close();
            const auto sockets = m_server->findChildren<QTcpSocket*>();
            for (QTcpSocket* s : sockets)
                s->abort();
            delete m_server;
        },
        Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

QString MockGoogleServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
}

QString MockGoogleServer::newId()
{
    return QStringLiteral("id%1").arg(m_nextId++);
}

QString MockGoogleServer::addFolder(const QString& name, const QString& parentId)
{
    QMutexLocker lock(&m_mutex);
    File f;
    f.id = newId();
    f.name = name;
    f.mimeType = kFolderMime;
    f.parent = parentId;
    f.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    m_files.insert(f.id, f);
    return f.id;
}

QString MockGoogleServer::addFile(const QString& name, const QString& parentId, const QByteArray& content,
    const QString& mimeType)
{
    QMutexLocker lock(&m_mutex);
    File f;
    f.id = newId();
    f.name = name;
    f.mimeType = mimeType;
    f.parent = parentId;
    f.content = content;
    f.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    m_files.insert(f.id, f);
    return f.id;
}

void MockGoogleServer::setContent(const QString& id, const QByteArray& content)
{
    QMutexLocker lock(&m_mutex);
    m_files[id].content = content;
    m_files[id].modifiedMs = QDateTime::currentMSecsSinceEpoch();
}

void MockGoogleServer::setTrashed(const QString& id, bool trashed)
{
    QMutexLocker lock(&m_mutex);
    m_files[id].trashed = trashed;
}

QString MockGoogleServer::idByPath(const QString& path, const QString& rootId) const
{
    QMutexLocker lock(&m_mutex);
    QString current = rootId;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        QString next;
        for (const File& f : m_files) {
            if (f.parent == current && f.name == part && !f.trashed) {
                next = f.id;
                break;
            }
        }
        if (next.isEmpty())
            return {};
        current = next;
    }
    return current;
}

bool MockGoogleServer::existsPath(const QString& path, const QString& rootId) const
{
    return !idByPath(path, rootId).isEmpty();
}

QByteArray MockGoogleServer::contentByPath(const QString& path, const QString& rootId) const
{
    const QString id = idByPath(path, rootId);
    QMutexLocker lock(&m_mutex);
    return m_files.value(id).content;
}

MockGoogleServer::File MockGoogleServer::file(const QString& id) const
{
    QMutexLocker lock(&m_mutex);
    return m_files.value(id);
}

QStringList MockGoogleServer::childNames(const QString& parentId) const
{
    QMutexLocker lock(&m_mutex);
    QStringList out;
    for (const File& f : m_files) {
        if (f.parent == parentId && !f.trashed)
            out.append(f.name);
    }
    out.sort();
    return out;
}

void MockGoogleServer::setAccountEmail(const QString& email)
{
    QMutexLocker lock(&m_mutex);
    m_email = email;
}

void MockGoogleServer::setDenyAuthorization(bool deny)
{
    QMutexLocker lock(&m_mutex);
    m_deny = deny;
}

void MockGoogleServer::expireAccessTokens()
{
    QMutexLocker lock(&m_mutex);
    m_accessTokens.clear();
}

void MockGoogleServer::invalidateRefreshTokens()
{
    QMutexLocker lock(&m_mutex);
    m_refreshTokens.clear();
    m_accessTokens.clear();
}

QStringList MockGoogleServer::revokedTokens() const
{
    QMutexLocker lock(&m_mutex);
    return m_revoked;
}

QStringList MockGoogleServer::issuedRefreshTokens() const
{
    QMutexLocker lock(&m_mutex);
    return m_issuedRefresh;
}

QMap<QString, QString> MockGoogleServer::lastAuthorizationQuery() const
{
    QMutexLocker lock(&m_mutex);
    return m_lastAuthQuery;
}

QMap<QString, QString> MockGoogleServer::lastTokenRequest() const
{
    QMutexLocker lock(&m_mutex);
    return m_lastTokenRequest;
}

QString MockGoogleServer::issueRefreshToken()
{
    QMutexLocker lock(&m_mutex);
    const QString token = QStringLiteral("rt-") + newId();
    m_refreshTokens.insert(token);
    m_issuedRefresh.append(token);
    return token;
}

void MockGoogleServer::setMaxPageSize(int n)
{
    QMutexLocker lock(&m_mutex);
    m_maxPageSize = n;
}

void MockGoogleServer::failNext(const QString& pathPart, int status, int count, const QByteArray& method)
{
    QMutexLocker lock(&m_mutex);
    m_faults.append({ pathPart, status, count, method });
}

void MockGoogleServer::setDownloadChunkDelayMs(int ms)
{
    QMutexLocker lock(&m_mutex);
    m_chunkDelayMs = ms;
}

void MockGoogleServer::interruptNextUploadAfter(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_interruptUploadAfter = bytes;
}

QStringList MockGoogleServer::requestLog() const
{
    QMutexLocker lock(&m_mutex);
    return m_log;
}

int MockGoogleServer::requestCount(const QString& contains) const
{
    QMutexLocker lock(&m_mutex);
    return int(std::count_if(m_log.cbegin(), m_log.cend(), [&](const QString& l) { return l.contains(contains); }));
}

void MockGoogleServer::clearRequestLog()
{
    QMutexLocker lock(&m_mutex);
    m_log.clear();
}

// ---------------------------------------------------------------- server side

void MockGoogleServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, m_server, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, m_server, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void MockGoogleServer::onReadyRead(QTcpSocket* socket)
{
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2) {
        socket->abort();
        return;
    }
    Request req;
    req.method = first.at(0);
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0)
            req.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
    }
    const qint64 length = req.headers.value("content-length").toLongLong();
    if (buffer.size() - (headerEnd + 4) < length)
        return; // wait for the rest of the body
    req.body = buffer.mid(headerEnd + 4, int(length));
    req.url = QUrl::fromEncoded("http://127.0.0.1" + first.at(1));
    req.path = req.url.path();
    m_buffers.remove(socket);
    socket->disconnect(m_server); // one request per connection
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    handle(socket, req);
}

void MockGoogleServer::reply(QTcpSocket* socket, int status, const QByteArray& body,
    const QList<QPair<QByteArray, QByteArray>>& headers, const QByteArray& contentType)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n";
    for (const auto& h : headers)
        out += h.first + ": " + h.second + "\r\n";
    out += "\r\n";
    socket->write(out);
    socket->write(body);
    socket->disconnectFromHost();
}

void MockGoogleServer::replyError(QTcpSocket* socket, int status, const QString& reason, const QString& message)
{
    QJsonObject item { { QStringLiteral("reason"), reason }, { QStringLiteral("message"), message } };
    QJsonObject error { { QStringLiteral("code"), status },
        { QStringLiteral("message"), message },
        { QStringLiteral("errors"), QJsonArray { item } } };
    reply(socket, status, QJsonDocument(QJsonObject { { QStringLiteral("error"), error } }).toJson());
}

void MockGoogleServer::replyThrottled(QTcpSocket* socket, const QByteArray& body)
{
    int delay = 0;
    {
        QMutexLocker lock(&m_mutex);
        delay = m_chunkDelayMs;
    }
    if (delay <= 0) {
        reply(socket, 200, body, {}, "application/octet-stream");
        return;
    }
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: "
        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
    auto* timer = new QTimer(socket);
    auto offset = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, socket, [socket, timer, body, offset]() {
        const int chunk = 16 * 1024;
        socket->write(body.mid(*offset, chunk));
        *offset += chunk;
        if (*offset >= body.size()) {
            timer->stop();
            socket->disconnectFromHost();
        }
    });
    timer->start(delay);
}

bool MockGoogleServer::authorized(const Request& request) const
{
    const QByteArray auth = request.headers.value("authorization");
    if (!auth.startsWith("Bearer "))
        return false;
    return m_accessTokens.contains(QString::fromUtf8(auth.mid(7)));
}

QByteArray MockGoogleServer::fileJson(const File& f) const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), f.id);
    o.insert(QStringLiteral("name"), f.name);
    o.insert(QStringLiteral("mimeType"), f.mimeType);
    o.insert(QStringLiteral("modifiedTime"),
        QDateTime::fromMSecsSinceEpoch(f.modifiedMs, Qt::UTC).toString(Qt::ISODateWithMs));
    if (!f.mimeType.startsWith(QLatin1String("application/vnd.google-apps."))) {
        o.insert(QStringLiteral("size"), QString::number(f.content.size()));
        o.insert(QStringLiteral("md5Checksum"),
            QString::fromLatin1(QCryptographicHash::hash(f.content, QCryptographicHash::Md5).toHex()));
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void MockGoogleServer::handle(QTcpSocket* socket, const Request& req)
{
    QMutexLocker lock(&m_mutex);
    m_log.append(QString::fromLatin1(req.method) + QLatin1Char(' ') + req.path);

    for (int i = 0; i < m_faults.size(); ++i) {
        Fault& fault = m_faults[i];
        if (req.path.contains(fault.pathPart) && (fault.method.isEmpty() || fault.method == req.method)) {
            const int status = fault.status;
            if (--fault.count <= 0)
                m_faults.removeAt(i);
            lock.unlock();
            replyError(socket, status, status == 403 ? QStringLiteral("rateLimitExceeded") : QStringLiteral("backendError"),
                QStringLiteral("Injected failure"));
            return;
        }
    }

    const QUrlQuery query(req.url);

    // ---- OAuth ----
    if (req.path == QLatin1String("/o/oauth2/v2/auth")) {
        handleAuthorize(socket, req);
        return;
    }
    if (req.path == QLatin1String("/token")) {
        handleToken(socket, req);
        return;
    }
    if (req.path == QLatin1String("/revoke")) {
        QString token = parseForm(req.body).value(QStringLiteral("token"));
        if (token.isEmpty())
            token = query.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded);
        m_revoked.append(token);
        m_refreshTokens.remove(token);
        m_accessTokens.clear();
        lock.unlock();
        reply(socket, 200, "{}");
        return;
    }

    // ---- upload session content (session URI carries its own authorization) ----
    if (req.path.startsWith(QLatin1String("/upload/session/"))) {
        handleUploadPut(socket, req, req.path.mid(16));
        return;
    }

    if (!authorized(req)) {
        lock.unlock();
        replyError(socket, 401, QStringLiteral("authError"), QStringLiteral("Invalid Credentials"));
        return;
    }

    // ---- Drive v3 ----
    if (req.path == QLatin1String("/drive/v3/about")) {
        const QJsonObject user { { QStringLiteral("emailAddress"), m_email },
            { QStringLiteral("displayName"), QStringLiteral("Test User") } };
        lock.unlock();
        reply(socket, 200, QJsonDocument(QJsonObject { { QStringLiteral("user"), user } }).toJson());
        return;
    }
    if (req.path == QLatin1String("/drive/v3/files") && req.method == "GET") {
        handleList(socket, req);
        return;
    }
    if (req.path == QLatin1String("/drive/v3/files") && req.method == "POST") {
        const QJsonObject meta = QJsonDocument::fromJson(req.body).object();
        File f;
        f.id = newId();
        f.name = meta.value(QStringLiteral("name")).toString();
        f.mimeType = meta.value(QStringLiteral("mimeType")).toString(QStringLiteral("application/octet-stream"));
        f.parent = meta.value(QStringLiteral("parents")).toArray().first().toString(QStringLiteral("root"));
        f.modifiedMs = QDateTime::currentMSecsSinceEpoch();
        m_files.insert(f.id, f);
        const QByteArray json = fileJson(f);
        lock.unlock();
        reply(socket, 200, json);
        return;
    }
    if (req.path.startsWith(QLatin1String("/drive/v3/files/"))) {
        const QString id = req.path.mid(16);
        if (!m_files.contains(id)) {
            lock.unlock();
            replyError(socket, 404, QStringLiteral("notFound"), QStringLiteral("File not found: %1.").arg(id));
            return;
        }
        File& f = m_files[id];
        if (req.method == "GET" && query.queryItemValue(QStringLiteral("alt")) == QLatin1String("media")) {
            const QByteArray content = f.content;
            lock.unlock();
            replyThrottled(socket, content);
            return;
        }
        if (req.method == "GET") {
            const QByteArray json = fileJson(f);
            lock.unlock();
            reply(socket, 200, json);
            return;
        }
        if (req.method == "PATCH") {
            const QJsonObject meta = QJsonDocument::fromJson(req.body).object();
            if (meta.contains(QStringLiteral("trashed")))
                f.trashed = meta.value(QStringLiteral("trashed")).toBool();
            if (meta.contains(QStringLiteral("name")))
                f.name = meta.value(QStringLiteral("name")).toString();
            const QByteArray json = fileJson(f);
            lock.unlock();
            reply(socket, 200, json);
            return;
        }
    }
    if (req.path.startsWith(QLatin1String("/upload/drive/v3/files"))
        && query.queryItemValue(QStringLiteral("uploadType")) == QLatin1String("resumable")) {
        const QJsonObject meta = QJsonDocument::fromJson(req.body).object();
        Session s;
        if (req.method == "PATCH") {
            s.fileId = req.path.mid(int(qstrlen("/upload/drive/v3/files/")));
            if (!m_files.contains(s.fileId)) {
                lock.unlock();
                replyError(socket, 404, QStringLiteral("notFound"), QStringLiteral("File not found"));
                return;
            }
        } else {
            s.name = meta.value(QStringLiteral("name")).toString();
            s.parent = meta.value(QStringLiteral("parents")).toArray().first().toString(QStringLiteral("root"));
        }
        const QDateTime modified = QDateTime::fromString(meta.value(QStringLiteral("modifiedTime")).toString(), Qt::ISODateWithMs);
        s.modifiedMs = modified.isValid() ? modified.toMSecsSinceEpoch() : QDateTime::currentMSecsSinceEpoch();
        s.total = req.headers.value("x-upload-content-length", "-1").toLongLong();
        const QString sid = newId();
        m_sessions.insert(sid, s);
        const QByteArray location = (baseUrl() + QStringLiteral("/upload/session/") + sid).toUtf8();
        lock.unlock();
        reply(socket, 200, {}, { { "Location", location } });
        return;
    }

    lock.unlock();
    replyError(socket, 404, QStringLiteral("notFound"), QStringLiteral("Unknown endpoint"));
}

void MockGoogleServer::handleAuthorize(QTcpSocket* socket, const Request& req)
{
    // called with m_mutex held
    const QUrlQuery q(req.url);
    m_lastAuthQuery.clear();
    const auto items = q.queryItems(QUrl::FullyDecoded);
    for (const auto& i : items)
        m_lastAuthQuery.insert(i.first, i.second);
    QUrl redirect(m_lastAuthQuery.value(QStringLiteral("redirect_uri")));
    QUrlQuery out;
    out.addQueryItem(QStringLiteral("state"), m_lastAuthQuery.value(QStringLiteral("state")));
    if (m_deny) {
        out.addQueryItem(QStringLiteral("error"), QStringLiteral("access_denied"));
    } else {
        const QString code = QStringLiteral("code-") + newId();
        m_codes.insert(code, { m_lastAuthQuery.value(QStringLiteral("code_challenge")),
                                 m_lastAuthQuery.value(QStringLiteral("redirect_uri")),
                                 m_lastAuthQuery.value(QStringLiteral("client_id")) });
        out.addQueryItem(QStringLiteral("code"), code);
    }
    redirect.setPath(QStringLiteral("/"));
    redirect.setQuery(out);
    const QByteArray location = redirect.toEncoded();
    m_mutex.unlock();
    reply(socket, 302, {}, { { "Location", location } });
    m_mutex.lock();
}

void MockGoogleServer::handleToken(QTcpSocket* socket, const Request& req)
{
    // called with m_mutex held
    const QMap<QString, QString> form = parseForm(req.body);
    m_lastTokenRequest = form;
    QJsonObject out;
    int status = 200;
    const auto error = [&](const QString& code, const QString& description) {
        status = 400;
        out = QJsonObject { { QStringLiteral("error"), code }, { QStringLiteral("error_description"), description } };
    };
    const QString grant = form.value(QStringLiteral("grant_type"));
    if (grant == QLatin1String("authorization_code")) {
        const QString code = form.value(QStringLiteral("code"));
        const AuthCode info = m_codes.take(code);
        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash(form.value(QStringLiteral("code_verifier")).toLatin1(), QCryptographicHash::Sha256)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        if (info.challenge.isEmpty() || info.challenge != expected) {
            error(QStringLiteral("invalid_grant"), QStringLiteral("Invalid code verifier."));
        } else if (info.redirectUri != form.value(QStringLiteral("redirect_uri"))) {
            error(QStringLiteral("redirect_uri_mismatch"), QStringLiteral("Bad Request"));
        } else if (info.clientId != form.value(QStringLiteral("client_id"))) {
            error(QStringLiteral("invalid_client"), QStringLiteral("Unknown client"));
        } else {
            const QString access = QStringLiteral("at-") + newId();
            const QString refresh = QStringLiteral("rt-") + newId();
            m_accessTokens.insert(access);
            m_refreshTokens.insert(refresh);
            m_issuedRefresh.append(refresh);
            out = QJsonObject { { QStringLiteral("access_token"), access },
                { QStringLiteral("refresh_token"), refresh },
                { QStringLiteral("expires_in"), 3920 },
                { QStringLiteral("token_type"), QStringLiteral("Bearer") },
                { QStringLiteral("scope"), QStringLiteral("https://www.googleapis.com/auth/drive") } };
        }
    } else if (grant == QLatin1String("refresh_token")) {
        if (!m_refreshTokens.contains(form.value(QStringLiteral("refresh_token")))) {
            error(QStringLiteral("invalid_grant"), QStringLiteral("Token has been expired or revoked."));
        } else {
            const QString access = QStringLiteral("at-") + newId();
            m_accessTokens.insert(access);
            out = QJsonObject { { QStringLiteral("access_token"), access },
                { QStringLiteral("expires_in"), 3920 },
                { QStringLiteral("token_type"), QStringLiteral("Bearer") } };
        }
    } else {
        error(QStringLiteral("unsupported_grant_type"), QStringLiteral("Bad grant type"));
    }
    m_mutex.unlock();
    reply(socket, status, QJsonDocument(out).toJson());
    m_mutex.lock();
}

void MockGoogleServer::handleList(QTcpSocket* socket, const Request& req)
{
    // called with m_mutex held
    const QUrlQuery query(req.url);
    const QString q = query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded);
    QString parent;
    const auto parentMatch = QRegularExpression(QStringLiteral("'((?:[^'\\\\]|\\\\.)*)' in parents")).match(q);
    if (parentMatch.hasMatch())
        parent = parentMatch.captured(1);
    const bool notTrashed = q.contains(QRegularExpression(QStringLiteral("trashed\\s*=\\s*false")));
    QString mime;
    const auto mimeMatch = QRegularExpression(QStringLiteral("mimeType\\s*=\\s*'([^']*)'")).match(q);
    if (mimeMatch.hasMatch())
        mime = mimeMatch.captured(1);

    QList<File> matches;
    for (const File& f : qAsConst(m_files)) {
        if (!parent.isEmpty() && f.parent != parent)
            continue;
        if (notTrashed && f.trashed)
            continue;
        if (!mime.isEmpty() && f.mimeType != mime)
            continue;
        matches.append(f);
    }
    std::stable_sort(matches.begin(), matches.end(), [](const File& a, const File& b) { return a.name < b.name; });

    const int pageSize = qMin(m_maxPageSize, qMax(1, query.queryItemValue(QStringLiteral("pageSize")).toInt()));
    const int offset = query.queryItemValue(QStringLiteral("pageToken")).toInt();
    QByteArray files;
    for (int i = offset; i < matches.size() && i < offset + pageSize; ++i) {
        if (!files.isEmpty())
            files += ',';
        files += fileJson(matches.at(i));
    }
    QByteArray body = "{\"files\":[" + files + "]";
    if (offset + pageSize < matches.size())
        body += ",\"nextPageToken\":\"" + QByteArray::number(offset + pageSize) + "\"";
    body += "}";
    m_mutex.unlock();
    reply(socket, 200, body);
    m_mutex.lock();
}

void MockGoogleServer::handleUploadPut(QTcpSocket* socket, const Request& req, const QString& sessionId)
{
    // called with m_mutex held
    if (!m_sessions.contains(sessionId)) {
        m_mutex.unlock();
        replyError(socket, 404, QStringLiteral("notFound"), QStringLiteral("Upload session expired"));
        m_mutex.lock();
        return;
    }
    Session& s = m_sessions[sessionId];
    const QByteArray range = req.headers.value("content-range");
    const auto incomplete = [&]() {
        QList<QPair<QByteArray, QByteArray>> headers;
        if (!s.data.isEmpty())
            headers.append({ "Range", "bytes=0-" + QByteArray::number(s.data.size() - 1) });
        m_mutex.unlock();
        reply(socket, 308, {}, headers);
        m_mutex.lock();
    };

    if (range.startsWith("bytes */")) { // status query
        if (s.total >= 0 && s.data.size() < s.total) {
            incomplete();
            return;
        }
    } else {
        qint64 start = 0;
        if (range.startsWith("bytes ")) {
            start = range.mid(6, range.indexOf('-') - 6).toLongLong();
            const qint64 total = range.mid(range.indexOf('/') + 1).toLongLong();
            if (total > 0)
                s.total = total;
        }
        if (start != s.data.size()) { // out of order: tell the client where we are
            incomplete();
            return;
        }
        if (m_interruptUploadAfter >= 0) {
            s.data += req.body.left(int(m_interruptUploadAfter));
            m_interruptUploadAfter = -1;
            m_mutex.unlock();
            socket->abort(); // connection lost in the middle of the upload
            m_mutex.lock();
            return;
        }
        s.data += req.body;
        if (s.total >= 0 && s.data.size() < s.total) {
            incomplete();
            return;
        }
    }

    File f;
    if (s.fileId.isEmpty()) {
        f.id = newId();
        f.name = s.name;
        f.parent = s.parent;
        f.mimeType = QStringLiteral("application/octet-stream");
    } else {
        f = m_files.value(s.fileId);
    }
    f.content = s.data;
    f.modifiedMs = s.modifiedMs;
    f.trashed = false;
    m_files.insert(f.id, f);
    m_sessions.remove(sessionId);
    const QByteArray json = fileJson(f);
    m_mutex.unlock();
    reply(socket, 200, json);
    m_mutex.lock();
}
