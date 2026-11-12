#pragma once

#include <QString>

namespace gds {

/// URLs of the Google services used by the application.
/// Defaults are the production endpoints from the Google documentation:
///  - https://developers.google.com/identity/protocols/oauth2/native-app
///  - https://developers.google.com/workspace/drive/api/reference/rest/v3
struct GoogleEndpoints {
    QString authUrl = QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth");
    QString tokenUrl = QStringLiteral("https://oauth2.googleapis.com/token");
    QString revokeUrl = QStringLiteral("https://oauth2.googleapis.com/revoke");
    QString apiBase = QStringLiteral("https://www.googleapis.com/drive/v3");
    QString uploadBase = QStringLiteral("https://www.googleapis.com/upload/drive/v3");
    /// Page the browser is sent on to once the sign-in has reached the application.
    QString signedInUrl = QStringLiteral("https://github.com/anjankow/gdrive-sync");

    /// Scope giving access to all files of the user. Required to sync folders
    /// that were not created by this application.
    static QString driveScope() { return QStringLiteral("https://www.googleapis.com/auth/drive"); }

    /// Endpoints of a test server listening on \p baseUrl (e.g. http://127.0.0.1:1234).
    static GoogleEndpoints forTestServer(const QString& baseUrl)
    {
        GoogleEndpoints e;
        e.authUrl = baseUrl + QStringLiteral("/o/oauth2/v2/auth");
        e.tokenUrl = baseUrl + QStringLiteral("/token");
        e.revokeUrl = baseUrl + QStringLiteral("/revoke");
        e.apiBase = baseUrl + QStringLiteral("/drive/v3");
        e.uploadBase = baseUrl + QStringLiteral("/upload/drive/v3");
        e.signedInUrl = baseUrl + QStringLiteral("/signed-in");
        return e;
    }
};

} // namespace gds
