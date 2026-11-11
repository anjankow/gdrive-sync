#pragma once

#include "endpoints.h"

#include <QDateTime>
#include <QMutex>
#include <QString>

namespace gds {

/// Thread-safe provider of OAuth access tokens. Refreshes the access token
/// with the refresh token when it expired. Blocking: call from worker threads.
class TokenSource {
public:
    TokenSource(GoogleEndpoints endpoints, QString clientId, QString clientSecret, QString refreshToken);

    /// Seeds a freshly obtained access token so that no refresh is needed at first.
    void setAccessToken(const QString& token, int expiresInSeconds);

    /// Returns a valid access token or an empty string (see \p error).
    /// \p forceRefresh discards the cached token (use after a 401).
    QString accessToken(QString* error = nullptr, bool forceRefresh = false);

    /// True when Google rejected the refresh token (revoked or expired):
    /// the user has to sign in again.
    bool authorizationLost() const;

    QString refreshToken() const { return m_refreshToken; }

private:
    const GoogleEndpoints m_endpoints;
    const QString m_clientId;
    const QString m_clientSecret;
    const QString m_refreshToken;
    mutable QMutex m_mutex;
    QString m_accessToken;
    QDateTime m_expiry;
    bool m_authLost = false;
};

} // namespace gds
