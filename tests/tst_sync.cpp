// Requirements:
//  - "the user can select which folder should be mounted from the drive, not the entire drive"
//  - "the files should be synced when requested by the user"
//  - sync direction chosen by the user, conflicts are asked, Google documents are skipped
#include "localscanner.h"
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestSync : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void onlyTheSelectedFolderIsSynced()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString sub = f.server.addFolder(QStringLiteral("sub"), docs);
        f.server.addFile(QStringLiteral("a.txt"), docs, "alpha");
        f.server.addFile(QStringLiteral("b.txt"), sub, "bravo");
        const QString other = f.server.addFolder(QStringLiteral("Private"));
        f.server.addFile(QStringLiteral("secret.txt"), other, "must not be downloaded");
        f.server.addFile(QStringLiteral("root.txt"), QStringLiteral("root"), "must not be downloaded either");

        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.downloaded, 2);
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(readFile(dir + QStringLiteral("/a.txt")), QByteArray("alpha"));
        QCOMPARE(readFile(dir + QStringLiteral("/sub/b.txt")), QByteArray("bravo"));
        // nothing from outside the selected folder arrived
        const LocalTree tree = scanLocalTree(dir);
        QCOMPARE(tree.keys(),
            (QStringList {QStringLiteral("a.txt"), QStringLiteral("sub"), QStringLiteral("sub/b.txt")}));
        // and Drive was only asked about the selected folder and its sub-folder
        QCOMPARE(f.server.requestCount(QStringLiteral("GET /drive/v3/files")), 4); // 2 listings + 2 downloads
    }

    void nothingHappensUntilTheUserRequestsASync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "alpha");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QTest::qWait(300);
        QVERIFY(!f.core->isSyncing());
        QVERIFY(QDir(f.localDir(QStringLiteral("Docs"))).isEmpty());
        QCOMPARE(f.server.requestCount(QStringLiteral("/drive/v3/files")), 0);

        QSignalSpy started(f.core.get(), &AppCore::syncStarted);
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QCOMPARE(started.count(), 1);
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/a.txt")));
    }

    void twoWaySyncPropagatesChangesInBothDirections()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::TwoWay));
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString remoteFile = f.server.addFile(QStringLiteral("remote.txt"), docs, "v1");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);

        // local additions (including a new folder) and a local edit
        QVERIFY(writeFile(dir + QStringLiteral("/new/local.txt"), "from local"));
        // a remote edit and a remote addition
        f.server.setContent(remoteFile, "v2 from drive");
        f.server.addFile(QStringLiteral("added.txt"), docs, "added on drive");

        SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.uploaded, 1);
        QCOMPARE(r.downloaded, 2);
        QCOMPARE(f.server.contentByPath(QStringLiteral("Docs/new/local.txt")), QByteArray("from local"));
        QCOMPARE(readFile(dir + QStringLiteral("/remote.txt")), QByteArray("v2 from drive"));
        QCOMPARE(readFile(dir + QStringLiteral("/added.txt")), QByteArray("added on drive"));

        // local edit of an existing file is uploaded as an update of the same Drive file
        QVERIFY(writeFile(dir + QStringLiteral("/remote.txt"), "v3 edited locally"));
        touchLater(dir + QStringLiteral("/remote.txt"));
        r = f.sync(id);
        QCOMPARE(r.uploaded, 1);
        QCOMPARE(f.server.file(remoteFile).content, QByteArray("v3 edited locally"));

        // a further sync without changes transfers nothing
        r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.uploaded + r.downloaded + r.deletedLocal + r.deletedRemote, 0);
    }

    // A Drive folder mapped to a local folder that already holds files.
    void nonEmptyFolderUploadsItsFilesOnTheFirstSync()
    {
        for (SyncDirection d :
            {SyncDirection::TwoWay, SyncDirection::LocalToDrive, SyncDirection::DriveToLocal}) {
            Fixture f;
            QVERIFY(f.startLoggedIn(d));
            const QString docs = f.server.addFolder(QStringLiteral("Docs"));
            f.server.addFile(QStringLiteral("on-drive.txt"), docs, "drive");
            const QString dir = f.localDir(QStringLiteral("Docs"));
            QVERIFY(writeFile(dir + QStringLiteral("/mine.txt"), "my file"));
            QVERIFY(writeFile(dir + QStringLiteral("/photos/2024/pic.jpg"), "jpeg"));
            const QString id = f.mount(docs, QStringLiteral("Docs"));
            QVERIFY(!id.isEmpty());

            const SyncResult r = f.sync(id);
            QCOMPARE(r.status, SyncResult::Success);
            const bool uploads = d != SyncDirection::DriveToLocal;
            const bool downloads = d != SyncDirection::LocalToDrive;
            QCOMPARE(f.server.existsPath(QStringLiteral("Docs/mine.txt")), uploads);
            QCOMPARE(f.server.existsPath(QStringLiteral("Docs/photos/2024/pic.jpg")), uploads);
            if (uploads) {
                QCOMPARE(f.server.contentByPath(QStringLiteral("Docs/mine.txt")), QByteArray("my file"));
                QCOMPARE(r.uploaded, 2);
            }
            QCOMPARE(QFile::exists(dir + QStringLiteral("/on-drive.txt")), downloads);
            // the local files are never removed
            QCOMPARE(readFile(dir + QStringLiteral("/mine.txt")), QByteArray("my file"));
            QCOMPARE(readFile(dir + QStringLiteral("/photos/2024/pic.jpg")), QByteArray("jpeg"));
            QCOMPARE(f.deletionQuestions, 0);

            // nothing left to do afterwards
            const SyncResult again = f.sync(id);
            QCOMPARE(again.uploaded + again.downloaded + again.deletedLocal + again.deletedRemote, 0);
        }
    }

    void differentFileWithTheSameNameIsAskedBeforeTheFirstSync()
    {
        struct Case {
            SyncDirection direction;
            ConflictChoice choice;
            QByteArray local; // expected afterwards
            QByteArray drive;
        };
        const QVector<Case> cases {
            {SyncDirection::DriveToLocal, ConflictChoice::KeepLocal, "local", "drive"},
            {SyncDirection::DriveToLocal, ConflictChoice::KeepRemote, "drive", "drive"},
            {SyncDirection::LocalToDrive, ConflictChoice::KeepLocal, "local", "local"},
            {SyncDirection::LocalToDrive, ConflictChoice::KeepRemote, "local", "drive"},
            {SyncDirection::TwoWay, ConflictChoice::KeepRemote, "drive", "drive"},
        };
        for (const Case& c : cases) {
            Fixture f;
            QVERIFY(f.startLoggedIn(c.direction));
            const QString docs = f.server.addFolder(QStringLiteral("Docs"));
            const QString onDrive = f.server.addFile(QStringLiteral("notes.txt"), docs, "drive");
            f.server.addFile(QStringLiteral("same.txt"), docs, "identical");
            const QString dir = f.localDir(QStringLiteral("Docs"));
            QVERIFY(writeFile(dir + QStringLiteral("/notes.txt"), "local"));
            QVERIFY(writeFile(dir + QStringLiteral("/same.txt"), "identical"));
            const QString id = f.mount(docs, QStringLiteral("Docs"));
            f.conflictChoice = c.choice;

            QCOMPARE(f.sync(id).status, SyncResult::Success);
            // asked in every direction, and only about the file that differs
            QCOMPARE(f.askedConflicts.size(), 1);
            QCOMPARE(f.askedConflicts.first().path, QStringLiteral("notes.txt"));
            QCOMPARE(f.askedConflicts.first().kind, ConflictKind::DifferentBeforeFirstSync);
            QCOMPARE(readFile(dir + QStringLiteral("/notes.txt")), c.local);
            QCOMPARE(f.server.file(onDrive).content, c.drive);

            // decided once: the next sync asks nothing and transfers nothing
            f.askedConflicts.clear();
            const SyncResult again = f.sync(id);
            QVERIFY(f.askedConflicts.isEmpty());
            QCOMPARE(again.uploaded + again.downloaded, 0);
        }
    }

    void theWholeOfMyDriveCanBeSynced()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::TwoWay));
        f.server.addFile(QStringLiteral("top.txt"), QStringLiteral("root"), "top");
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("inner.txt"), docs, "inner");
        const QString id = f.mount(QStringLiteral("root"), QStringLiteral("My Drive"));
        QVERIFY(!id.isEmpty());
        const QString dir = f.localDir(QStringLiteral("My Drive"));

        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QCOMPARE(readFile(dir + QStringLiteral("/top.txt")), QByteArray("top"));
        QCOMPARE(readFile(dir + QStringLiteral("/Docs/inner.txt")), QByteArray("inner"));

        // new local files land at the top of My Drive and in its folders
        QVERIFY(writeFile(dir + QStringLiteral("/local-top.txt"), "lt"));
        QVERIFY(writeFile(dir + QStringLiteral("/Docs/local-inner.txt"), "li"));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.uploaded, 2);
        QCOMPARE(f.server.contentByPath(QStringLiteral("local-top.txt")), QByteArray("lt"));
        QCOMPARE(f.server.contentByPath(QStringLiteral("Docs/local-inner.txt")), QByteArray("li"));
    }

    void driveToLocalNeverUploads()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::DriveToLocal));
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString a = f.server.addFile(QStringLiteral("a.txt"), docs, "v1");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);

        QVERIFY(writeFile(dir + QStringLiteral("/local-only.txt"), "stays local"));
        QVERIFY(writeFile(dir + QStringLiteral("/a.txt"), "local edit!"));
        touchLater(dir + QStringLiteral("/a.txt"));
        SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.uploaded, 0);
        QVERIFY(!f.server.existsPath(QStringLiteral("Docs/local-only.txt")));
        QCOMPARE(f.server.file(a).content, QByteArray("v1"));
        QCOMPARE(f.server.requestCount(QStringLiteral("/upload/")), 0);

        // a change on Drive wins over the local edit
        f.server.setContent(a, "v2 drive");
        r = f.sync(id);
        QCOMPARE(r.downloaded, 1);
        QCOMPARE(readFile(dir + QStringLiteral("/a.txt")), QByteArray("v2 drive"));
    }

    void localToDriveNeverDownloads()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::LocalToDrive));
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("drive-only.txt"), docs, "stays on drive");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QVERIFY(writeFile(dir + QStringLiteral("/mine.txt"), "upload me"));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.downloaded, 0);
        QCOMPARE(r.uploaded, 1);
        QVERIFY(!QFile::exists(dir + QStringLiteral("/drive-only.txt")));
        QCOMPARE(f.server.contentByPath(QStringLiteral("Docs/mine.txt")), QByteArray("upload me"));
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/drive-only.txt")));
    }

    void newFoldersGetTheDefaultDirectionWhichIsTwoWayUnlessChanged()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QCOMPARE(f.core->defaultDirection(), SyncDirection::TwoWay);
        const QString a = f.mount(f.server.addFolder(QStringLiteral("A")), QStringLiteral("A"));
        QCOMPARE(f.core->folderDirection(a), SyncDirection::TwoWay);

        f.core->setDefaultDirection(SyncDirection::DriveToLocal);
        const QString b = f.mount(f.server.addFolder(QStringLiteral("B")), QStringLiteral("B"));
        QCOMPARE(f.core->folderDirection(b), SyncDirection::DriveToLocal);
        // the default only applies to folders added afterwards
        QCOMPARE(f.core->folderDirection(a), SyncDirection::TwoWay);

        // everything survives a restart
        f.core->flushSettings();
        f.createCore();
        QVERIFY(f.initialize());
        QCOMPARE(f.core->defaultDirection(), SyncDirection::DriveToLocal);
        QCOMPARE(f.core->folderDirection(a), SyncDirection::TwoWay);
        QCOMPARE(f.core->folderDirection(b), SyncDirection::DriveToLocal);
    }

    void eachFolderSyncsInItsOwnDirection()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString up = f.server.addFolder(QStringLiteral("Up"));
        const QString down = f.server.addFolder(QStringLiteral("Down"));
        f.server.addFile(QStringLiteral("drive.txt"), up, "d");
        f.server.addFile(QStringLiteral("drive.txt"), down, "d");
        const QString upId = f.mount(up, QStringLiteral("Up"));
        const QString downId = f.mount(down, QStringLiteral("Down"));
        QVERIFY(f.core->setFolderDirection(upId, SyncDirection::LocalToDrive));
        QVERIFY(f.core->setFolderDirection(downId, SyncDirection::DriveToLocal));
        QVERIFY(writeFile(f.localDir(QStringLiteral("Up")) + QStringLiteral("/local.txt"), "l"));
        QVERIFY(writeFile(f.localDir(QStringLiteral("Down")) + QStringLiteral("/local.txt"), "l"));

        QVERIFY(f.core->syncAll());
        QCOMPARE(f.waitForSync().size(), 2);
        // Up: local to Drive only
        QVERIFY(f.server.existsPath(QStringLiteral("Up/local.txt")));
        QVERIFY(!QFile::exists(f.localDir(QStringLiteral("Up")) + QStringLiteral("/drive.txt")));
        // Down: Drive to local only
        QVERIFY(!f.server.existsPath(QStringLiteral("Down/local.txt")));
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("Down")) + QStringLiteral("/drive.txt")));
    }

    void directionOfAMappedFolderCanBeChangedLater()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::DriveToLocal));
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QVERIFY(writeFile(dir + QStringLiteral("/local.txt"), "l"));
        QCOMPARE(f.sync(id).uploaded, 0); // Drive to local never uploads

        QVERIFY(f.core->setFolderDirection(id, SyncDirection::TwoWay));
        QCOMPARE(f.sync(id).uploaded, 1); // applies from the next sync
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/local.txt")));
        QVERIFY(f.logText().contains(QStringLiteral("set to Two-way")));

        // unknown folders and a running sync are refused
        QVERIFY(!f.core->setFolderDirection(QStringLiteral("nope"), SyncDirection::TwoWay));
        f.server.addFile(QStringLiteral("big.bin"), docs, QByteArray(2000000, 'x'));
        f.server.setDownloadChunkDelayMs(10);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(id));
        QString error;
        QVERIFY(!f.core->setFolderDirection(id, SyncDirection::LocalToDrive, &error));
        QVERIFY(error.contains(QStringLiteral("sync is running")));
        QCOMPARE(f.core->folderDirection(id), SyncDirection::TwoWay);
        f.core->cancelSync();
        QVERIFY(finished.count() > 0 || finished.wait(10000));
    }

    void directionOfEarlierVersionsIsKept()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        f.core->flushSettings();
        f.core.reset();
        {
            // as written by versions with one direction for the whole sign-in
            KConfig config(f.paths().configFile, KConfig::SimpleConfig);
            config.group("Account").writeEntry("direction", int(SyncDirection::LocalToDrive));
            config.group("Sync").deleteEntry("defaultDirection");
            config.group("Folders").group(id).deleteEntry("direction");
        }
        f.createCore();
        QVERIFY(f.initialize());
        QCOMPARE(f.core->folderDirection(id), SyncDirection::LocalToDrive);
        QCOMPARE(f.core->defaultDirection(), SyncDirection::LocalToDrive);
    }

    void googleDocumentsAreSkipped()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("Report"), docs, QByteArray(),
            QStringLiteral("application/vnd.google-apps.document"));
        f.server.addFile(QStringLiteral("real.txt"), docs, "real");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.downloaded, 1);
        QCOMPARE(QDir(f.localDir(QStringLiteral("Docs"))).entryList(QDir::Files),
            QStringList {QStringLiteral("real.txt")});
        QVERIFY(r.skipped.join(QLatin1Char('\n')).contains(QStringLiteral("Report")));
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/Report")));
    }

    void conflictIsAskedAndKeepBothKeepsBothVersions()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn(SyncDirection::TwoWay));
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString a = f.server.addFile(QStringLiteral("a.txt"), docs, "base");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);

        QVERIFY(writeFile(dir + QStringLiteral("/a.txt"), "local change"));
        touchLater(dir + QStringLiteral("/a.txt"));
        f.server.setContent(a, "remote change");
        f.conflictChoice = ConflictChoice::KeepBoth;
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(f.askedConflicts.size(), 1);
        QCOMPARE(f.askedConflicts.first().path, QStringLiteral("a.txt"));
        QCOMPARE(f.askedConflicts.first().kind, ConflictKind::BothModified);

        QCOMPARE(readFile(dir + QStringLiteral("/a.txt")), QByteArray("remote change"));
        const QStringList copies
            = QDir(dir).entryList({QStringLiteral("a (conflict copy*.txt")}, QDir::Files);
        QCOMPARE(copies.size(), 1);
        QCOMPARE(readFile(dir + QLatin1Char('/') + copies.first()), QByteArray("local change"));
        QCOMPARE(
            f.server.contentByPath(QStringLiteral("Docs/") + copies.first()), QByteArray("local change"));
        QCOMPARE(f.server.file(a).content, QByteArray("remote change"));
    }

    void conflictChoicesKeepLocalKeepRemoteAndSkip()
    {
        for (const ConflictChoice choice :
            {ConflictChoice::KeepLocal, ConflictChoice::KeepRemote, ConflictChoice::Skip}) {
            Fixture f;
            QVERIFY(f.startLoggedIn(SyncDirection::TwoWay));
            const QString docs = f.server.addFolder(QStringLiteral("Docs"));
            const QString a = f.server.addFile(QStringLiteral("a.txt"), docs, "base");
            const QString id = f.mount(docs, QStringLiteral("Docs"));
            const QString path = f.localDir(QStringLiteral("Docs")) + QStringLiteral("/a.txt");
            QCOMPARE(f.sync(id).status, SyncResult::Success);
            QVERIFY(writeFile(path, "local change"));
            touchLater(path);
            f.server.setContent(a, "remote change");
            f.conflictChoice = choice;
            QCOMPARE(f.sync(id).status, SyncResult::Success);
            const QByteArray expectedLocal
                = choice == ConflictChoice::KeepRemote ? "remote change" : "local change";
            const QByteArray expectedRemote
                = choice == ConflictChoice::KeepLocal ? "local change" : "remote change";
            QCOMPARE(readFile(path), expectedLocal);
            QCOMPARE(f.server.file(a).content, expectedRemote);
        }
    }

    void missingLocalFolderIsAnErrorNotAMassDeletion()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "alpha");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QVERIFY(QDir(f.localDir(QStringLiteral("Docs"))).removeRecursively());
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Failed);
        QCOMPARE(f.deletionQuestions, 0);
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/a.txt")));
    }

    void lostAuthorizationIsReported()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        f.server.invalidateRefreshTokens();
        QSignalSpy lost(f.core.get(), &AppCore::authorizationLost);
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::AuthRequired);
        QCOMPARE(lost.count(), 1);
    }

    void failedItemDoesNotStopTheOthers()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString bad = f.server.addFile(QStringLiteral("bad.txt"), docs, "x");
        f.server.addFile(QStringLiteral("good.txt"), docs, "good");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        f.server.failNext(QStringLiteral("/drive/v3/files/") + bad, 404, 1);
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Failed);
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.downloaded, 1);
        QCOMPARE(
            readFile(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/good.txt")), QByteArray("good"));
        // the failed file is picked up by the next sync
        const SyncResult again = f.sync(id);
        QCOMPARE(again.status, SyncResult::Success);
        QCOMPARE(again.downloaded, 1);
    }

    void syncCanBeCancelled()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("big.bin"), docs, QByteArray(3000000, 'q'));
        f.server.setDownloadChunkDelayMs(20);
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(id));
        QTRY_VERIFY(!f.progressEvents.isEmpty() && f.progressEvents.last().bytesDone > 0);
        f.core->cancelSync();
        QVERIFY(finished.wait(5000));
        const auto results = finished.first().at(0).value<QVector<SyncResult>>();
        QCOMPARE(results.first().status, SyncResult::Cancelled);
        // no partial file is left behind
        QVERIFY(QDir(f.localDir(QStringLiteral("Docs"))).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "tst_sync.moc"
