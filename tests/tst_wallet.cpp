// The KDE wallet is used only while an action needs it: a sync, the Drive
// folder browser, signing in or out, or changing the Google API client. The
// application never asks to open it by itself, because every request to open
// a closed wallet shows the password prompt.
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestWallet : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void startingSignedInDoesNotOpenTheWallet()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QVERIFY(f.core->isLoggedIn());
        QCOMPARE(f.core->accountEmail(), f.server.accountEmail());
        QTest::qWait(200);
        QCOMPARE(f.secrets.openCount, 0);
        QVERIFY(!f.secrets.isOpen());
    }

    void aSyncOpensTheWalletAndClosesItAfterwards()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "a");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QCOMPARE(f.secrets.openCount, 1);
        QTRY_VERIFY(!f.secrets.isOpen()); // closed once nothing needs it any more
        QCOMPARE(f.secrets.closeCount, 1);

        // idle: the wallet is not opened again by itself
        QTest::qWait(300);
        QCOMPARE(f.secrets.openCount, 1);

        // the next sync opens it again
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QCOMPARE(f.secrets.openCount, 2);
        QTRY_VERIFY(!f.secrets.isOpen());
    }

    void walletClosedByTheSystemIsNotReopened()
    {
        Fixture f;
        f.core.reset();
        QVERIFY(f.startLoggedIn());
        f.core->setSecretsLingerMs(60000); // the application would keep it open for a while
        const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QVERIFY(f.secrets.isOpen());
        f.secrets.close(); // e.g. the idle timeout of the wallet service
        QTest::qWait(300);
        QCOMPARE(f.secrets.openCount, 1); // no new password prompt
        // and the next action opens it again
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QCOMPARE(f.secrets.openCount, 2);
    }

    void browsingDriveFoldersUsesOneWalletOpening()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.core->setSecretsLingerMs(2000);
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFolder(QStringLiteral("Sub"), docs);
        QSignalSpy listed(f.core.get(), &AppCore::driveFoldersListed);
        f.core->listDriveFolders(QStringLiteral("root"));
        QVERIFY(listed.wait(5000));
        QCOMPARE(listed.last().at(2).toString(), QString());
        f.core->listDriveFolders(docs); // the user expands a folder
        QVERIFY(listed.wait(5000));
        QCOMPARE(listed.last().at(1).value<QVector<DriveItem>>().size(), 1);
        QCOMPARE(f.secrets.openCount, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!f.secrets.isOpen(), 5000);
    }

    void aClosedWalletFailsTheSyncWithTheReason()
    {
        Fixture f;
        f.secrets.failOpen = true; // the user dismissed the password prompt
        QVERIFY(f.startLoggedIn());
        const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        const SyncResult result = f.sync(id);
        QCOMPARE(result.status, SyncResult::Failed);
        QVERIFY(result.errorMessage.contains(QStringLiteral("KDE wallet could not be opened")));
        QVERIFY(f.core->isLoggedIn()); // still signed in: the next sync asks again
        QVERIFY(!f.core->isSyncing());
    }

    void cancellingWhileThePasswordIsAskedStopsTheSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(id)); // the wallet is still being opened
        f.core->cancelSync();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<QVector<SyncResult>>().first().status, SyncResult::Cancelled);
        QVERIFY(!f.core->isSyncing());
        QTest::qWait(200); // the wallet answer that arrives later starts nothing
        QCOMPARE(f.server.requestCount(QStringLiteral("GET /drive/v3/files")), 0);
        QTRY_VERIFY(!f.secrets.isOpen());
    }

    void signOutRemovesTheSignInAndClosesTheWallet()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.core->logout();
        QVERIFY(!f.core->isLoggedIn());
        QTRY_VERIFY(f.secrets.stored(SecretKeys::refreshToken()).isEmpty());
        QTRY_VERIFY(!f.secrets.isOpen());
    }

    void failedRemovalAtSignOutIsRepeatedNextTime()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.secrets.failOpen = true;
        f.core->logout();
        QTRY_VERIFY(f.logText().contains(QStringLiteral("could not be removed from the KDE wallet")));
        QVERIFY(!f.secrets.stored(SecretKeys::refreshToken()).isEmpty());
        // later the wallet opens for another action (here: the API client dialog)
        f.secrets.failOpen = false;
        const Lease lease = f.openSecrets();
        QVERIFY(lease);
        QVERIFY(f.secrets.stored(SecretKeys::refreshToken()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWallet)
#include "tst_wallet.moc"
