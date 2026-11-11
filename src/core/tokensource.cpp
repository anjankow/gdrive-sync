#include "tokensource.h"

#include "http.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>

namespace gds {

TokenSource::TokenSource(GoogleEndpoints endpoints, QString clientId, QString clientSecret, QString refreshToken)
    : m_endpoints(std::move(endpoints))
    , m_clientId(std::move(clientId))
    , m_clientSecret(std::move(clientSecret))
    , m_refreshToken(std::move(refreshToken))
{
}

void TokenSource::setAccessToken(const QString& token, int expiresInSeconds)
{
    QMutexLocker lock(&m_mutex);
    m_accessToken = token;
    m_expiry = QDateTime::currentDateTimeUtc().addSecs(qMax(0, expiresInSeconds - 60));
}

bool TokenSource::authorizationLost() const
{
    QMutexLocker lock(&m_mutex);
    return m_authLost;
}

QString TokenSource::accessToken(QString* error, bool forceRefresh)
{
    QMutexLocker lock(&m_mutex);
    if (!forceRefresh && !m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_expiry)
        return m_accessToken;
    if (m_authLost) {
        if (error)
            *error = QStringLiteral("Authorization was revoked or expired; please sign in again");
        return {};
    }

    QList<QPair<QString, QString>> fields {
        { QStringLiteral("client_id"), m_clientId },
        { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
        { QStringLiteral("refresh_token"), m_refreshToken },
    };
    if (!m_clientSecret.isEmpty())
        fields.append({ QStringLiteral("client_secret"), m_clientSecret });

    HttpRequest req;
    req.method = "POST";
    req.url = QUrl(m_endpoints.tokenUrl);
    req.headers.append(HttpHeader("Content-Type", "application/x-www-form-urlencoded"));
    req.body = formEncode(fields);
    req.transferTimeoutMs = 30000;

    QNetworkAccessManager nam;
    const HttpResponse resp = httpSend(nam, req);
    const QJsonObject json = QJsonDocument::fromJson(resp.body).object();
    if (resp.status != 200) {
        const QString code = json.value(QStringLiteral("error")).toString();
        if (code == QLatin1String("invalid_grant")) {
            m_authLost = true;
            m_accessToken.clear();
            if (error)
                *error = QStringLiteral("Authorization was revoked or expired; please sign in again");
        } else if (error) {
            *error = QStringLiteral("Could not refresh the access token: %1")
                         .arg(code.isEmpty() ? resp.errorString : code);
        }
        return {};
    }
    m_accessToken = json.value(QStringLiteral("access_token")).toString();
    const int expiresIn = json.value(QStringLiteral("expires_in")).toInt(3600);
    m_expiry = QDateTime::currentDateTimeUtc().addSecs(qMax(0, expiresIn - 60));
    if (m_accessToken.isEmpty() && error)
        *error = QStringLiteral("Google returned an empty access token");
    return m_accessToken;
}

} // namespace gds
