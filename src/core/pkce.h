#pragma once

#include <QByteArray>
#include <QString>

namespace gds {

/// PKCE helpers (RFC 7636) as required by Google for installed applications.
struct Pkce {
    /// Random verifier of 43..128 unreserved characters.
    static QString generateVerifier(int length = 64);
    /// BASE64URL-ENCODE(SHA256(ASCII(verifier))) without padding.
    static QString challengeS256(const QString& verifier);
    /// Random URL-safe string, used for the OAuth "state" parameter.
    static QString randomToken(int bytes = 24);
};

} // namespace gds
