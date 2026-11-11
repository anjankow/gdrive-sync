// Tests of the Drive REST v3 client against the mock server.
#include "driveclient.h"
#include "localscanner.h"
#include "testutil.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTimer>

using namespace gds;
using namespace gdstest;

class TestDriveClient : public QObject {
    Q_OBJECT
    std::unique_ptr<MockGoogleServer> m_server;
    std::shared_ptr<TokenSource> m_tokens;
    std::unique_ptr<DriveClient> m_client;
    QTemporaryDir m_tmp;

private Q_SLOTS:
    void init()
    {
        m_server = std::make_unique<MockGoogleServer>();
        const auto endpoints = GoogleEndpoints::forTestServer(m_server->baseUrl());
        m_tokens = std::make_shared<TokenSource>(
            endpoints, QStringLiteral("cid"), QStringLiteral("secret"), m_server->issueRefreshToken());
        m_client = std::make_unique<DriveClient>(endpoints, m_tokens);
        m_client->setRetryBaseDelayMs(5);
    }
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aboutReturnsAccount()
    {
        m_server->setAccountEmail(QStringLiteral("someone@example.com"));
        const auto about = m_client->about();
        QVERIFY(about.ok());
        QCOMPARE(about.value.email, QStringLiteral("someone@example.com"));
    }

    void listsChildrenAcrossPages()
    {
        const QString folder = m_server->addFolder(QStringLiteral("big"));
        for (int i = 0; i < 25; ++i)
            m_server->addFile(
                QStringLiteral("f%1.txt").arg(i, 2, 10, QLatin1Char('0')), folder, QByteArray(i, 'x'));
        m_server->addFile(QStringLiteral("elsewhere.txt"), QStringLiteral("root"), "no");
        m_server->setMaxPageSize(10);
        const auto list = m_client->listChildren(folder);
        QVERIFY(list.ok());
        QCOMPARE(list.value.size(), 25);
        QCOMPARE(m_server->requestCount(QStringLiteral("GET /drive/v3/files")), 3);
        QCOMPARE(list.value.at(3).name, QStringLiteral("f03.txt"));
        QCOMPARE(list.value.at(3).size, 3);
        QCOMPARE(list.value.at(3).md5,
            QString::fromLatin1(QCryptographicHash::hash("xxx", QCryptographicHash::Md5).toHex()));
        QVERIFY(list.value.at(3).modifiedMs > 0);
    }

    void listsOnlyFoldersAndSkipsTrashed()
    {
        const QString a = m_server->addFolder(QStringLiteral("A"));
        const QString b = m_server->addFolder(QStringLiteral("B"));
        m_server->addFile(QStringLiteral("file.txt"), QStringLiteral("root"), "x");
        m_server->setTrashed(b);
        const auto list = m_client->listChildren(QStringLiteral("root"), true);
        QVERIFY(list.ok());
        QCOMPARE(list.value.size(), 1);
        QCOMPARE(list.value.first().id, a);
        QVERIFY(list.value.first().isFolder());
    }

    void quotesInFolderIdsAreEscaped()
    {
        QCOMPARE(DriveClient::escapeQueryValue(QStringLiteral("it's\\")), QStringLiteral("it\\'s\\\\"));
    }

    void downloadsContent()
    {
        QByteArray content(300000, Qt::Uninitialized);
        for (int i = 0; i < content.size(); ++i)
            content[i] = char(i * 7);
        const QString id = m_server->addFile(QStringLiteral("d.bin"), QStringLiteral("root"), content);
        const QString dest = m_tmp.path() + QStringLiteral("/d.bin");
        qint64 last = 0;
        const DriveError e = m_client->download(id, dest, [&](qint64 done, qint64) { last = done; });
        QVERIFY2(e.ok(), qPrintable(e.message));
        QCOMPARE(readFile(dest), content);
        QCOMPARE(last, content.size());
    }

    void downloadOfMissingFileFailsAndLeavesNoFile()
    {
        const QString dest = m_tmp.path() + QStringLiteral("/missing.bin");
        const DriveError e = m_client->download(QStringLiteral("nope"), dest);
        QCOMPARE(e.kind, DriveError::NotFound);
        QVERIFY(!QFile::exists(dest));
    }

    void uploadCreatesAndUpdates()
    {
        const QString folder = m_server->addFolder(QStringLiteral("up"));
        const QString path = m_tmp.path() + QStringLiteral("/u.txt");
        QVERIFY(writeFile(path, "first version"));
        auto created = m_client->upload(path, QStringLiteral("u.txt"), folder, QString(), 1700000000123LL);
        QVERIFY2(created.ok(), qPrintable(created.error.message));
        QCOMPARE(m_server->contentByPath(QStringLiteral("up/u.txt")), QByteArray("first version"));
        QCOMPARE(created.value.md5, fileMd5(path));
        QCOMPARE(m_server->file(created.value.id).modifiedMs, 1700000000123LL);
        // create = POST, as documented for resumable uploads
        QCOMPARE(m_server->requestCount(QStringLiteral("POST /upload/drive/v3/files")), 1);

        QVERIFY(writeFile(path, "second, longer version"));
        auto updated = m_client->upload(path, QStringLiteral("u.txt"), folder, created.value.id, 0);
        QVERIFY(updated.ok());
        QCOMPARE(updated.value.id, created.value.id);
        QCOMPARE(m_server->contentByPath(QStringLiteral("up/u.txt")), QByteArray("second, longer version"));
        // update = PATCH
        QCOMPARE(
            m_server->requestCount(QStringLiteral("PATCH /upload/drive/v3/files/") + created.value.id), 1);
        QCOMPARE(m_server->childNames(folder).size(), 1);
    }

    void uploadsEmptyFile()
    {
        const QString path = m_tmp.path() + QStringLiteral("/empty.txt");
        QVERIFY(writeFile(path, ""));
        const auto created
            = m_client->upload(path, QStringLiteral("empty.txt"), QStringLiteral("root"), QString(), 0);
        QVERIFY2(created.ok(), qPrintable(created.error.message));
        QVERIFY(m_server->existsPath(QStringLiteral("empty.txt")));
        QCOMPARE(created.value.size, 0);
    }

    void interruptedUploadIsResumed()
    {
        QByteArray content(700000, Qt::Uninitialized);
        for (int i = 0; i < content.size(); ++i)
            content[i] = char(i % 251);
        const QString path = m_tmp.path() + QStringLiteral("/big.bin");
        QVERIFY(writeFile(path, content));
        m_server->interruptNextUploadAfter(262144);
        const auto result
            = m_client->upload(path, QStringLiteral("big.bin"), QStringLiteral("root"), QString(), 0);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(m_server->contentByPath(QStringLiteral("big.bin")), content);
        // more than one PUT was needed: the upload was resumed, not restarted from a new session
        QVERIFY(m_server->requestCount(QStringLiteral("PUT /upload/session/")) >= 2);
        QCOMPARE(m_server->requestCount(QStringLiteral("POST /upload/drive/v3/files")), 1);
    }

    void createsFolderAndTrashes()
    {
        const auto folder = m_client->createFolder(QStringLiteral("New"), QStringLiteral("root"));
        QVERIFY(folder.ok());
        QVERIFY(folder.value.isFolder());
        QVERIFY(m_server->existsPath(QStringLiteral("New")));
        QVERIFY(m_client->trash(folder.value.id).ok());
        QVERIFY(!m_server->existsPath(QStringLiteral("New")));
        QVERIFY(m_server->file(folder.value.id).trashed); // trashed, not permanently deleted
    }

    void retriesTransientErrors()
    {
        m_server->addFolder(QStringLiteral("A"));
        m_server->failNext(QStringLiteral("/drive/v3/files"), 500, 2);
        m_server->failNext(QStringLiteral("/drive/v3/files"), 429, 1);
        m_server->failNext(QStringLiteral("/drive/v3/files"), 403, 1); // rateLimitExceeded
        const auto list = m_client->listChildren(QStringLiteral("root"));
        QVERIFY2(list.ok(), qPrintable(list.error.message));
        QCOMPARE(list.value.size(), 1);
        QCOMPARE(m_server->requestCount(QStringLiteral("GET /drive/v3/files")), 5);
    }

    void givesUpAfterMaxRetries()
    {
        m_client->setMaxRetries(2);
        m_server->failNext(QStringLiteral("/drive/v3/files"), 503, 10);
        const auto list = m_client->listChildren(QStringLiteral("root"));
        QCOMPARE(list.error.kind, DriveError::Api);
        QCOMPARE(list.error.httpStatus, 503);
        QCOMPARE(m_server->requestCount(QStringLiteral("GET /drive/v3/files")), 3);
    }

    void refreshesExpiredAccessToken()
    {
        QVERIFY(m_client->about().ok());
        m_server->expireAccessTokens();
        QVERIFY(m_client->about().ok());
        QCOMPARE(m_server->requestCount(QStringLiteral("POST /token")), 2);
    }

    void reportsLostAuthorization()
    {
        m_server->invalidateRefreshTokens();
        const auto about = m_client->about();
        QCOMPARE(about.error.kind, DriveError::Auth);
        QVERIFY(m_tokens->authorizationLost());
    }

    void cancelStopsARunningDownload()
    {
        const QString id
            = m_server->addFile(QStringLiteral("slow.bin"), QStringLiteral("root"), QByteArray(2000000, 'z'));
        m_server->setDownloadChunkDelayMs(20);
        std::atomic_bool cancel {false};
        DriveClient client(GoogleEndpoints::forTestServer(m_server->baseUrl()), m_tokens, &cancel);
        QTimer::singleShot(200, [&cancel]() { cancel.store(true); });
        QElapsedTimer timer;
        timer.start();
        const QString dest = m_tmp.path() + QStringLiteral("/slow.bin");
        const DriveError e = client.download(id, dest);
        QCOMPARE(e.kind, DriveError::Cancelled);
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(!QFile::exists(dest));
    }
};

QTEST_GUILESS_MAIN(TestDriveClient)
#include "tst_driveclient.moc"
