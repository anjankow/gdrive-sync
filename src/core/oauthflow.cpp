#include "oauthflow.h"

#include "http.h"
#include "pkce.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

namespace gds {

OAuthFlow::OAuthFlow(GoogleEndpoints endpoints, QString clientId, QString clientSecret, BrowserOpener opener,
    QObject* parent)
    : QObject(parent)
    , m_endpoints(std::move(endpoints))
    , m_clientId(std::move(clientId))
    , m_clientSecret(std::move(clientSecret))
    , m_opener(std::move(opener))
{
    qRegisterMetaType<gds::OAuthTokens>();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this,
        [this]() { finishFailed(QStringLiteral("Timed out waiting for the browser sign-in")); });
}

OAuthFlow::~OAuthFlow() = default;

void OAuthFlow::start()
{
    m_done = false;
    if (m_clientId.isEmpty()) {
        finishFailed(QStringLiteral("No OAuth client ID configured"));
        return;
    }
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &OAuthFlow::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        finishFailed(QStringLiteral("Could not open a local port for the sign-in redirect: %1")
                         .arg(m_server->errorString()));
        return;
    }

    m_verifier = Pkce::generateVerifier();
    m_state = Pkce::randomToken();
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());

    QUrlQuery q;
    const auto add = [&q](const QString& k, const QString& v) {
        q.addQueryItem(k, QString::fromUtf8(QUrl::toPercentEncoding(v)));
    };
    add(QStringLiteral("client_id"), m_clientId);
    add(QStringLiteral("redirect_uri"), m_redirectUri);
    add(QStringLiteral("response_type"), QStringLiteral("code"));
    add(QStringLiteral("scope"), GoogleEndpoints::driveScope());
    add(QStringLiteral("code_challenge"), Pkce::challengeS256(m_verifier));
    add(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    add(QStringLiteral("state"), m_state);
    // Always show the account chooser / consent so that a different account
    // can be used and a refresh token is issued again after a previous logout.
    add(QStringLiteral("prompt"), QStringLiteral("consent select_account"));
    m_authUrl = QUrl(m_endpoints.authUrl);
    m_authUrl.setQuery(q.query(QUrl::FullyEncoded), QUrl::StrictMode);

    m_timeout.start(m_timeoutMs);
    if (!m_opener || !m_opener(m_authUrl))
        finishFailed(QStringLiteral("Could not open the web browser"));
}

void OAuthFlow::cancel()
{
    finishFailed(QStringLiteral("Sign-in cancelled"));
}

void OAuthFlow::stopListening()
{
    m_timeout.stop();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
}

void OAuthFlow::finishFailed(const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    stopListening();
    Q_EMIT failed(error);
}

void OAuthFlow::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            if (!socket->canReadLine())
                return;
            const QByteArray line = socket->readLine();
            socket->disconnect(this);
            handleRequest(socket, line);
        });
    }
}

static void respond(QTcpSocket* socket, const QByteArray& status, const QString& text)
{
    const QByteArray html = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                                           "<title>Google Drive Sync</title></head>"
                                           "<body style=\"font-family:sans-serif;margin:3em\">"
                                           "<h2>Google Drive Sync</h2><p>%1</p></body></html>")
                                .arg(text.toHtmlEscaped())
                                .toUtf8();
    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
        + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html);
    socket->disconnectFromHost();
}

void OAuthFlow::handleRequest(QTcpSocket* socket, const QByteArray& requestLine)
{
    // "GET /?state=..&code=.. HTTP/1.1"
    const QList<QByteArray> parts = requestLine.trimmed().split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        respond(socket, "400 Bad Request", QStringLiteral("Unexpected request."));
        return;
    }
    const QUrl url = QUrl::fromEncoded("http://127.0.0.1" + parts.at(1));
    const QUrlQuery query(url);
    if (!query.hasQueryItem(QStringLiteral("code")) && !query.hasQueryItem(QStringLiteral("error"))) {
        // e.g. a favicon request; ignore and keep waiting
        respond(socket, "404 Not Found", QStringLiteral("Not found."));
        return;
    }
    if (m_done) {
        respond(socket, "400 Bad Request", QStringLiteral("This sign-in attempt is no longer active."));
        return;
    }
    if (query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded) != m_state) {
        respond(socket, "400 Bad Request", QStringLiteral("Invalid state parameter. Sign-in rejected."));
        finishFailed(QStringLiteral("Sign-in response did not match the request (state mismatch)"));
        return;
    }
    if (query.hasQueryItem(QStringLiteral("error"))) {
        const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
        respond(socket, "200 OK", QStringLiteral("Sign-in was not completed (%1). You can close this page.").arg(error));
        finishFailed(QStringLiteral("Google sign-in failed: %1").arg(error));
        return;
    }
    respond(socket, "200 OK", QStringLiteral("Sign-in complete. You can close this page and return to the application."));
    stopListening();
    exchangeCode(query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded));
}

void OAuthFlow::exchangeCode(const QString& code)
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QList<QPair<QString, QString>> fields {
        { QStringLiteral("client_id"), m_clientId },
        { QStringLiteral("code"), code },
        { QStringLiteral("code_verifier"), m_verifier },
        { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
        { QStringLiteral("redirect_uri"), m_redirectUri },
    };
    if (!m_clientSecret.isEmpty())
        fields.append({ QStringLiteral("client_secret"), m_clientSecret });

    QNetworkRequest req { QUrl(m_endpoints.tokenUrl) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(30000);
    QNetworkReply* reply = m_nam->post(req, formEncode(fields));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200) {
            QString detail = json.value(QStringLiteral("error_description")).toString();
            if (detail.isEmpty())
                detail = json.value(QStringLiteral("error")).toString();
            if (detail.isEmpty())
                detail = reply->errorString();
            finishFailed(QStringLiteral("Token request failed: %1").arg(detail));
            return;
        }
        OAuthTokens tokens;
        tokens.accessToken = json.value(QStringLiteral("access_token")).toString();
        tokens.refreshToken = json.value(QStringLiteral("refresh_token")).toString();
        tokens.expiresIn = json.value(QStringLiteral("expires_in")).toInt();
        if (tokens.accessToken.isEmpty() || tokens.refreshToken.isEmpty()) {
            finishFailed(QStringLiteral("Google did not return the expected tokens"));
            return;
        }
        if (m_done)
            return;
        m_done = true;
        m_timeout.stop();
        Q_EMIT succeeded(tokens);
    });
}

} // namespace gds
