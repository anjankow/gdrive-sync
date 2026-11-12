#pragma once

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QUrl>

class QTcpServer;
class QTcpSocket;

/// In-process fake of the Google OAuth 2.0 and Drive v3 REST endpoints used
/// by the application. Runs in its own thread; all public methods are
/// thread-safe and meant to be called from the test thread.
class MockGoogleServer : public QObject {
    Q_OBJECT
public:
    struct File {
        QString id;
        QString name;
        QString mimeType;
        QString parent;
        QByteArray content;
        qint64 modifiedMs = 0;
        bool trashed = false;
        bool isFolder() const { return mimeType == QLatin1String("application/vnd.google-apps.folder"); }
    };
    struct Request {
        QByteArray method;
        QString path; ///< path without query
        QUrl url;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    explicit MockGoogleServer(QObject* parent = nullptr);
    ~MockGoogleServer() override;

    QString baseUrl() const;

    // ---- Drive content ----
    QString addFolder(const QString& name, const QString& parentId = QStringLiteral("root"));
    QString addFile(const QString& name, const QString& parentId, const QByteArray& content,
        const QString& mimeType = QStringLiteral("application/octet-stream"));
    void setContent(const QString& id, const QByteArray& content);
    void setTrashed(const QString& id, bool trashed = true);
    /// Resolves "a/b/c.txt" below \p rootId among non-trashed items; empty when missing.
    QString idByPath(const QString& path, const QString& rootId = QStringLiteral("root")) const;
    bool existsPath(const QString& path, const QString& rootId = QStringLiteral("root")) const;
    QByteArray contentByPath(const QString& path, const QString& rootId = QStringLiteral("root")) const;
    File file(const QString& id) const;
    QStringList childNames(const QString& parentId) const;

    // ---- accounts / OAuth ----
    void setAccountEmail(const QString& email);
    void setAccountName(const QString& name);
    /// Whether the account has a profile picture (served at /photo).
    void setAccountHasPhoto(bool hasPhoto);
    static QByteArray photoPng();
    QString accountEmail() const { return m_email; }
    QString accountName() const { return m_name; }
    /// Profile picture URL as Drive reports it; empty without a picture.
    QString accountPhotoLink() const { return m_hasPhoto ? baseUrl() + QStringLiteral("/photo") : QString(); }
    void setDenyAuthorization(bool deny);
    void expireAccessTokens();
    void invalidateRefreshTokens();
    QStringList revokedTokens() const;
    QStringList issuedRefreshTokens() const;
    QMap<QString, QString> lastAuthorizationQuery() const;
    QMap<QString, QString> lastTokenRequest() const;
    /// Issues a refresh token directly (for tests that skip the browser flow).
    QString issueRefreshToken();

    // ---- behaviour / fault injection ----
    void setMaxPageSize(int n);
    /// The next \p count requests whose path contains \p pathPart answer with \p status.
    void failNext(const QString& pathPart, int status, int count = 1, const QByteArray& method = {});
    /// Delay between 16 KiB chunks of downloads (slow network simulation).
    void setDownloadChunkDelayMs(int ms);
    /// Every request is answered only after this delay (slow server simulation).
    void setResponseDelayMs(int ms);
    /// The next upload PUT is cut off after \p bytes were stored.
    void interruptNextUploadAfter(qint64 bytes);

    QStringList requestLog() const; ///< "METHOD /path"
    int requestCount(const QString& contains) const;
    void clearRequestLog();

private:
    struct Session {
        QString fileId; ///< empty: create
        QString name;
        QString parent;
        qint64 modifiedMs = 0;
        QByteArray data;
        qint64 total = -1;
    };
    struct AuthCode {
        QString challenge;
        QString redirectUri;
        QString clientId;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void handle(QTcpSocket* socket, const Request& request);
    void reply(QTcpSocket* socket, int status, const QByteArray& body = {},
        const QList<QPair<QByteArray, QByteArray>>& headers = {},
        const QByteArray& contentType = "application/json");
    void replyError(QTcpSocket* socket, int status, const QString& reason, const QString& message);
    void replyThrottled(QTcpSocket* socket, const QByteArray& body);
    bool authorized(const Request& request) const;
    QByteArray fileJson(const File& f) const;
    QString newId();

    void handleAuthorize(QTcpSocket* socket, const Request& request);
    void handleToken(QTcpSocket* socket, const Request& request);
    void handleList(QTcpSocket* socket, const Request& request);
    void handleUploadPut(QTcpSocket* socket, const Request& request, const QString& sessionId);

    QThread m_thread;
    QTcpServer* m_server = nullptr;
    quint16 m_port = 0;

    mutable QMutex m_mutex;
    QMap<QString, File> m_files;
    QHash<QString, Session> m_sessions;
    QHash<QString, AuthCode> m_codes;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QSet<QString> m_accessTokens;
    QSet<QString> m_refreshTokens;
    QStringList m_issuedRefresh;
    QStringList m_revoked;
    QMap<QString, QString> m_lastAuthQuery;
    QMap<QString, QString> m_lastTokenRequest;
    QString m_email = QStringLiteral("tester@example.com");
    QString m_name = QStringLiteral("Test User");
    bool m_hasPhoto = true;
    bool m_deny = false;
    int m_maxPageSize = 1000;
    int m_chunkDelayMs = 0;
    int m_responseDelayMs = 0;
    qint64 m_interruptUploadAfter = -1;
    struct Fault {
        QString pathPart;
        int status;
        int count;
        QByteArray method;
    };
    QList<Fault> m_faults;
    QStringList m_log;
    int m_nextId = 1;
};
