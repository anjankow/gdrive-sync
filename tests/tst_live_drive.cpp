// Optional tests against the real Google Drive API. Google provides no
// sandbox or test account, so these only run when credentials of a
// dedicated test account are supplied through the environment:
//
//   GDRIVESYNC_LIVE_CLIENT_ID, GDRIVESYNC_LIVE_CLIENT_SECRET, GDRIVESYNC_LIVE_REFRESH_TOKEN
//
// They are skipped otherwise (always in CI). Everything is created inside a
// uniquely named folder which is moved to the Drive trash at the end.
#include "driveclient.h"
#include "localscanner.h"
#include "testutil.h"

#include <QUuid>

using namespace gds;
using namespace gdstest;

class TestLiveDrive : public QObject {
    Q_OBJECT
    std::shared_ptr<TokenSource> m_tokens;
    std::unique_ptr<DriveClient> m_client;
    QTemporaryDir m_tmp;
    QString m_folderId;

private Q_SLOTS:
    void initTestCase()
    {
        const QString id = qEnvironmentVariable("GDRIVESYNC_LIVE_CLIENT_ID");
        const QString secret = qEnvironmentVariable("GDRIVESYNC_LIVE_CLIENT_SECRET");
        const QString refresh = qEnvironmentVariable("GDRIVESYNC_LIVE_REFRESH_TOKEN");
        if (id.isEmpty() || refresh.isEmpty())
            QSKIP("Live Google Drive credentials not provided (GDRIVESYNC_LIVE_* environment variables)");
        m_tokens = std::make_shared<TokenSource>(GoogleEndpoints(), id, secret, refresh);
        m_client = std::make_unique<DriveClient>(GoogleEndpoints(), m_tokens);
        const auto folder = m_client->createFolder(
            QStringLiteral("gdrive-sync-live-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces),
            QStringLiteral("root"));
        QVERIFY2(folder.ok(), qPrintable(folder.error.message));
        m_folderId = folder.value.id;
    }

    void cleanupTestCase()
    {
        if (m_client && !m_folderId.isEmpty())
            m_client->trash(m_folderId);
    }

    void aboutReturnsTheAccount()
    {
        const auto about = m_client->about();
        QVERIFY2(about.ok(), qPrintable(about.error.message));
        QVERIFY(about.value.email.contains(QLatin1Char('@')));
    }

    void uploadListDownloadUpdateTrash()
    {
        QByteArray content(6 * 1024 * 1024 + 123, Qt::Uninitialized); // > 5 MB: needs a resumable upload
        for (int i = 0; i < content.size(); ++i)
            content[i] = char((i * 31) % 253);
        const QString local = m_tmp.path() + QStringLiteral("/live.bin");
        QVERIFY(writeFile(local, content));

        qint64 uploaded = 0;
        const auto created = m_client->upload(local, QStringLiteral("live.bin"), m_folderId, QString(),
            1700000000000LL, [&](qint64 done, qint64) { uploaded = done; });
        QVERIFY2(created.ok(), qPrintable(created.error.message));
        QCOMPARE(uploaded, content.size());
        QCOMPARE(created.value.md5, fileMd5(local));
        QCOMPARE(created.value.size, content.size());
        QCOMPARE(created.value.modifiedMs, 1700000000000LL);

        const auto sub = m_client->createFolder(QStringLiteral("sub"), m_folderId);
        QVERIFY(sub.ok());
        auto list = m_client->listChildren(m_folderId);
        QVERIFY2(list.ok(), qPrintable(list.error.message));
        QCOMPARE(list.value.size(), 2);
        const auto folders = m_client->listChildren(m_folderId, true);
        QCOMPARE(folders.value.size(), 1);
        QVERIFY(folders.value.first().isFolder());

        const QString back = m_tmp.path() + QStringLiteral("/back.bin");
        const DriveError e = m_client->download(created.value.id, back);
        QVERIFY2(e.ok(), qPrintable(e.message));
        QCOMPARE(fileMd5(back), created.value.md5);

        QVERIFY(writeFile(local, "replaced"));
        const auto updated
            = m_client->upload(local, QStringLiteral("live.bin"), m_folderId, created.value.id, 0);
        QVERIFY2(updated.ok(), qPrintable(updated.error.message));
        QCOMPARE(updated.value.id, created.value.id);
        QCOMPARE(updated.value.size, 8);

        QVERIFY(m_client->trash(created.value.id).ok());
        list = m_client->listChildren(m_folderId);
        QCOMPARE(list.value.size(), 1); // trashed files are not listed
    }

    void emptyFileCanBeUploaded()
    {
        const QString local = m_tmp.path() + QStringLiteral("/empty.txt");
        QVERIFY(writeFile(local, ""));
        const auto created = m_client->upload(local, QStringLiteral("empty.txt"), m_folderId, QString(), 0);
        QVERIFY2(created.ok(), qPrintable(created.error.message));
        QCOMPARE(created.value.size, 0);
    }
};

QTEST_GUILESS_MAIN(TestLiveDrive)
#include "tst_live_drive.moc"
