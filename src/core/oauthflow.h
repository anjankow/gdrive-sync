#pragma once

#include "endpoints.h"

#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

namespace gds {

struct OAuthTokens {
    QString accessToken;
    QString refreshToken;
    int expiresIn = 0;
};

/// OAuth 2.0 authorization-code flow for installed apps: system browser,
/// loopback redirect (http://127.0.0.1:port) and PKCE (S256).
/// Fully asynchronous; lives in the GUI thread without blocking it.
class OAuthFlow : public QObject {
    Q_OBJECT
public:
    using BrowserOpener = std::function<bool(const QUrl&)>;

    OAuthFlow(GoogleEndpoints endpoints, QString clientId, QString clientSecret, BrowserOpener opener,
        QObject* parent = nullptr);
    ~OAuthFlow() override;

    void start();
    void cancel();
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    /// The URL handed to the browser (valid after start()).
    QUrl authorizationUrl() const { return m_authUrl; }

Q_SIGNALS:
    void succeeded(const gds::OAuthTokens& tokens);
    void failed(const QString& error);

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket, const QByteArray& requestLine);
    void exchangeCode(const QString& code);
    void finishFailed(const QString& error);
    void stopListening();

    GoogleEndpoints m_endpoints;
    QString m_clientId;
    QString m_clientSecret;
    BrowserOpener m_opener;
    QTcpServer* m_server = nullptr;
    QNetworkAccessManager* m_nam = nullptr;
    QTimer m_timeout;
    int m_timeoutMs = 5 * 60 * 1000;
    QString m_verifier;
    QString m_state;
    QString m_redirectUri;
    QUrl m_authUrl;
    bool m_done = false;
};

} // namespace gds

Q_DECLARE_METATYPE(gds::OAuthTokens)
