// Requirement: "In case of files deletion, the user is always warned which
// files will be removed on sync and the sync can be cancelled if needed."
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestDeletions : public QObject {
    Q_OBJECT

    struct Setup {
        QString folderId;
        QString dir;
        QString keepId, goneId, subFileId;
    };

    static Setup prepare(Fixture& f, SyncDirection direction = SyncDirection::TwoWay)
    {
        Setup s;
        if (!f.startLoggedIn(direction))
            return s;
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString sub = f.server.addFolder(QStringLiteral("sub"), docs);
        s.keepId = f.server.addFile(QStringLiteral("keep.txt"), docs, "keep");
        s.goneId = f.server.addFile(QStringLiteral("gone.txt"), docs, "gone");
        s.subFileId = f.server.addFile(QStringLiteral("in-sub.txt"), sub, "sub");
        s.folderId = f.mount(docs, QStringLiteral("Docs"));
        s.dir = f.localDir(QStringLiteral("Docs"));
        f.sync(s.folderId);
        return s;
    }

private Q_SLOTS:
    void firstSyncWithoutDeletionsDoesNotAsk()
    {
        Fixture f;
        const Setup s = prepare(f);
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        QCOMPARE(f.deletionQuestions, 0);
    }

    void remoteDeletionIsListedBeforeLocalFilesAreRemoved()
    {
        Fixture f;
        const Setup s = prepare(f);
        f.server.setTrashed(s.goneId);
        const SyncResult r = f.sync(s.folderId);
        QCOMPARE(f.deletionQuestions, 1);
        QCOMPARE(f.askedLocalDeletions, QStringList {QStringLiteral("gone.txt")});
        QVERIFY(f.askedRemoteDeletions.isEmpty());
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.deletedLocal, 1);
        QVERIFY(!QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/keep.txt")));
    }

    void localDeletionIsListedBeforeDriveFilesAreTrashed()
    {
        Fixture f;
        const Setup s = prepare(f);
        QVERIFY(QDir(s.dir + QStringLiteral("/sub")).removeRecursively());
        QVERIFY(QFile::remove(s.dir + QStringLiteral("/gone.txt")));
        const SyncResult r = f.sync(s.folderId);
        QCOMPARE(f.deletionQuestions, 1);
        QVERIFY(f.askedLocalDeletions.isEmpty());
        QCOMPARE(f.askedRemoteDeletions,
            (QStringList {
                QStringLiteral("sub/in-sub.txt"), QStringLiteral("gone.txt"), QStringLiteral("sub/")}));
        QCOMPARE(r.status, SyncResult::Success);
        QVERIFY(!f.server.existsPath(QStringLiteral("Docs/gone.txt")));
        QVERIFY(!f.server.existsPath(QStringLiteral("Docs/sub")));
        // moved to the Drive trash, not destroyed
        QVERIFY(f.server.file(s.goneId).trashed);
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/keep.txt")));
    }

    void decliningTheWarningCancelsTheWholeSync()
    {
        Fixture f;
        const Setup s = prepare(f);
        f.server.setTrashed(s.goneId);
        f.server.addFile(QStringLiteral("new.txt"), f.server.idByPath(QStringLiteral("Docs")), "new");
        QVERIFY(QFile::remove(s.dir + QStringLiteral("/keep.txt")));
        f.confirmDeletions = false;
        const SyncResult r = f.sync(s.folderId);
        QCOMPARE(f.deletionQuestions, 1);
        QCOMPARE(r.status, SyncResult::Cancelled);
        // nothing at all was changed: no deletion and no transfer
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/keep.txt")));
        QVERIFY(!QFile::exists(s.dir + QStringLiteral("/new.txt")));
        QCOMPARE(r.downloaded + r.uploaded + r.deletedLocal + r.deletedRemote, 0);

        // the warning comes again on the next sync and can then be accepted
        f.confirmDeletions = true;
        const SyncResult again = f.sync(s.folderId);
        QCOMPARE(f.deletionQuestions, 2);
        QCOMPARE(again.status, SyncResult::Success);
        QVERIFY(!QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        QVERIFY(!f.server.existsPath(QStringLiteral("Docs/keep.txt")));
    }

    void cancellingWhileTheWarningIsShownAbortsTheSync()
    {
        Fixture f;
        const Setup s = prepare(f);
        f.server.setTrashed(s.goneId);
        // replace the automatic answer: leave the question open and cancel instead
        QObject::disconnect(f.core.get(), &AppCore::deletionsPending, nullptr, nullptr);
        QSignalSpy asked(f.core.get(), &AppCore::deletionsPending);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(s.folderId));
        QVERIFY(asked.wait(10000));
        QTest::qWait(200);
        QVERIFY(f.core->isSyncing()); // the engine waits for the answer
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        f.core->cancelSync();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(0).value<QVector<SyncResult>>().first().status, SyncResult::Cancelled);
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/gone.txt")));
    }

    void oneWayDeletionsAreWarnedToo()
    {
        {
            Fixture f;
            const Setup s = prepare(f, SyncDirection::DriveToLocal);
            f.server.setTrashed(s.goneId);
            f.sync(s.folderId);
            QCOMPARE(f.askedLocalDeletions, QStringList {QStringLiteral("gone.txt")});
        }
        {
            Fixture f;
            const Setup s = prepare(f, SyncDirection::LocalToDrive);
            QVERIFY(writeFile(s.dir + QStringLiteral("/mine.txt"), "mine"));
            QCOMPARE(f.sync(s.folderId).uploaded, 1);
            QVERIFY(QFile::remove(s.dir + QStringLiteral("/mine.txt")));
            const SyncResult r = f.sync(s.folderId);
            QCOMPARE(f.askedRemoteDeletions, QStringList {QStringLiteral("mine.txt")});
            QCOMPARE(r.deletedRemote, 1);
            QVERIFY(!f.server.existsPath(QStringLiteral("Docs/mine.txt")));
        }
    }

    void deletionChosenInAConflictIsAlsoWarned()
    {
        Fixture f;
        const Setup s = prepare(f);
        // deleted on Drive, modified locally -> conflict; user keeps the Drive state (= delete locally)
        f.server.setTrashed(s.goneId);
        QVERIFY(writeFile(s.dir + QStringLiteral("/gone.txt"), "edited locally"));
        touchLater(s.dir + QStringLiteral("/gone.txt"));
        f.conflictChoice = ConflictChoice::KeepRemote;
        const SyncResult r = f.sync(s.folderId);
        QCOMPARE(f.askedConflicts.size(), 1);
        QCOMPARE(f.askedConflicts.first().kind, ConflictKind::LocalModifiedRemoteDeleted);
        QCOMPARE(f.deletionQuestions, 1);
        QCOMPARE(f.askedLocalDeletions, QStringList {QStringLiteral("gone.txt")});
        QCOMPARE(r.status, SyncResult::Success);
        QVERIFY(!QFile::exists(s.dir + QStringLiteral("/gone.txt")));
    }

    void unattendedSyncNeverDeletesAndNotifies()
    {
        Fixture f;
        const Setup s = prepare(f);
        f.server.setTrashed(s.goneId);
        QVERIFY(QFile::remove(s.dir + QStringLiteral("/keep.txt")));
        f.server.addFile(QStringLiteral("new.txt"), f.server.idByPath(QStringLiteral("Docs")), "new");

        const SyncResult r = f.sync(s.folderId, /*interactive=*/false);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(f.deletionQuestions, 0); // nobody is there to answer
        QCOMPARE(r.pendingDeletions, 2);
        QVERIFY(r.needsAttention());
        // deletions were skipped on both sides ...
        QVERIFY(QFile::exists(s.dir + QStringLiteral("/gone.txt")));
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/keep.txt")));
        // ... everything else was synced ...
        QCOMPARE(readFile(s.dir + QStringLiteral("/new.txt")), QByteArray("new"));
        // ... and the user is notified
        QCOMPARE(f.notifier.events, QStringList {QStringLiteral("syncAttention")});
        QVERIFY(f.notifier.texts.first().contains(QStringLiteral("2 deletion")));

        // the pending deletions are presented by the next manual sync
        const SyncResult manual = f.sync(s.folderId);
        QCOMPARE(f.deletionQuestions, 1);
        QCOMPARE(f.askedLocalDeletions, QStringList {QStringLiteral("gone.txt")});
        QCOMPARE(f.askedRemoteDeletions, QStringList {QStringLiteral("keep.txt")});
        QCOMPARE(manual.status, SyncResult::Success);
    }

    void driveFolderWithGoogleDocumentsIsNotTrashed()
    {
        Fixture f;
        const Setup s = prepare(f);
        f.server.addFile(QStringLiteral("Notes"), f.server.idByPath(QStringLiteral("Docs/sub")), QByteArray(),
            QStringLiteral("application/vnd.google-apps.document"));
        QVERIFY(QDir(s.dir + QStringLiteral("/sub")).removeRecursively());
        const SyncResult r = f.sync(s.folderId);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(f.askedRemoteDeletions, QStringList {QStringLiteral("sub/in-sub.txt")});
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/sub/Notes")));
        QVERIFY(!f.server.existsPath(QStringLiteral("Docs/sub/in-sub.txt")));
    }
};

QTEST_GUILESS_MAIN(TestDeletions)
#include "tst_deletions.moc"
