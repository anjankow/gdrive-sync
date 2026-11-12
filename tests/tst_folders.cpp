// Requirements:
//  - "The user can select where the drive folder should be mounted. The destination must be empty."
//  - several folders, each synced and unmounted independently; unmount deletes the local copy
//  - the Drive folder is chosen from the list of Drive folders
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestFolders : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void destinationMustBeEmpty()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        FolderManager* fm = f.core->folders();
        const QString base = f.tmp.path();

        QVERIFY(!fm->validateDestination(QString()).isEmpty());
        QVERIFY(!fm->validateDestination(QStringLiteral("relative/path")).isEmpty());

        QVERIFY(QDir().mkpath(base + QStringLiteral("/empty")));
        QCOMPARE(fm->validateDestination(base + QStringLiteral("/empty")), QString());
        // a destination that does not exist yet is created on mount
        QCOMPARE(fm->validateDestination(base + QStringLiteral("/does/not/exist/yet")), QString());

        QVERIFY(writeFile(base + QStringLiteral("/full/file.txt"), "x"));
        QVERIFY(fm->validateDestination(base + QStringLiteral("/full"))
                    .contains(QStringLiteral("must be empty")));
        QVERIFY(writeFile(base + QStringLiteral("/hidden/.dotfile"), "x"));
        QVERIFY(fm->validateDestination(base + QStringLiteral("/hidden"))
                    .contains(QStringLiteral("must be empty")));
        QVERIFY(QDir().mkpath(base + QStringLiteral("/withdir/sub")));
        QVERIFY(fm->validateDestination(base + QStringLiteral("/withdir"))
                    .contains(QStringLiteral("must be empty")));
        QVERIFY(!fm->validateDestination(base + QStringLiteral("/full/file.txt")).isEmpty()); // not a folder
    }

    void mountingIntoANonEmptyFolderIsRefused()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        QVERIFY(writeFile(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/already-here.txt"), "x"));
        QString error;
        QVERIFY(f.core->folders()
                    ->addFolder(docs, QStringLiteral("Docs"), f.localDir(QStringLiteral("Docs")), &error)
                    .isEmpty());
        QVERIFY(error.contains(QStringLiteral("must be empty")));
        QVERIFY(f.core->folders()->folders().isEmpty());
        QVERIFY(f.places.places.isEmpty());
        QCOMPARE(readFile(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/already-here.txt")),
            QByteArray("x"));
    }

    void mountCreatesTheDestinationAndAPlacesEntry()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QVERIFY(!id.isEmpty());
        QVERIFY(QFileInfo(f.localDir(QStringLiteral("Docs"))).isDir());
        // visible in the file manager
        QCOMPARE(f.places.places.value(f.localDir(QStringLiteral("Docs"))), QStringLiteral("Docs"));
        const FolderConfig* cfg = f.core->folders()->find(id);
        QVERIFY(cfg);
        QCOMPARE(cfg->driveFolderId, docs);
        QCOMPARE(cfg->localPath, f.localDir(QStringLiteral("Docs")));
    }

    void overlappingDestinationsAndDuplicateMountsAreRefused()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        const QString b = f.server.addFolder(QStringLiteral("B"));
        QVERIFY(!f.mount(a, QStringLiteral("A")).isEmpty());
        FolderManager* fm = f.core->folders();
        QString error;
        QVERIFY(fm->addFolder(b, QStringLiteral("B"), f.localDir(QStringLiteral("A")), &error).isEmpty());
        QVERIFY(fm->addFolder(b, QStringLiteral("B"),
                      f.localDir(QStringLiteral("A")) + QStringLiteral("/inside"), &error)
                    .isEmpty());
        QVERIFY(error.contains(QStringLiteral("overlaps")));
        QVERIFY(
            fm->addFolder(b, QStringLiteral("B"), f.tmp.path() + QStringLiteral("/local"), &error).isEmpty());
        QVERIFY(fm->addFolder(a, QStringLiteral("A"), f.localDir(QStringLiteral("A2")), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("already mounted")));
        QCOMPARE(fm->folders().size(), 1);
    }

    void driveFoldersAreListedForSelection()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFolder(QStringLiteral("Photos"));
        f.server.addFolder(QStringLiteral("2024"), docs);
        f.server.addFile(QStringLiteral("not-a-folder.txt"), QStringLiteral("root"), "x");

        QSignalSpy listed(f.core.get(), &AppCore::driveFoldersListed);
        f.core->listDriveFolders(QStringLiteral("root"));
        QVERIFY(listed.wait(10000));
        QCOMPARE(listed.first().at(0).toString(), QStringLiteral("root"));
        auto items = listed.first().at(1).value<QVector<DriveItem>>();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).name, QStringLiteral("Docs"));
        QCOMPARE(items.at(1).name, QStringLiteral("Photos"));

        listed.clear();
        f.core->listDriveFolders(docs); // sub-folders are loaded on demand
        QVERIFY(listed.wait(10000));
        items = listed.first().at(1).value<QVector<DriveItem>>();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().name, QStringLiteral("2024"));
    }

    void severalFoldersAreSyncedSeparately()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        const QString b = f.server.addFolder(QStringLiteral("B"));
        f.server.addFile(QStringLiteral("a.txt"), a, "in a");
        f.server.addFile(QStringLiteral("b.txt"), b, "in b");
        const QString idA = f.mount(a, QStringLiteral("A"));
        const QString idB = f.mount(b, QStringLiteral("B"));

        QCOMPARE(f.sync(idA).status, SyncResult::Success);
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("A")) + QStringLiteral("/a.txt")));
        QVERIFY(QDir(f.localDir(QStringLiteral("B"))).isEmpty()); // B was not requested

        QCOMPARE(f.sync(idB).status, SyncResult::Success);
        QCOMPARE(readFile(f.localDir(QStringLiteral("B")) + QStringLiteral("/b.txt")), QByteArray("in b"));
        QVERIFY(!QFile::exists(f.localDir(QStringLiteral("A")) + QStringLiteral("/b.txt")));
    }

    void syncAllSyncsEveryFolder()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        const QString b = f.server.addFolder(QStringLiteral("B"));
        f.server.addFile(QStringLiteral("a.txt"), a, "in a");
        f.server.addFile(QStringLiteral("b.txt"), b, "in b");
        f.mount(a, QStringLiteral("A"));
        f.mount(b, QStringLiteral("B"));
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncAll());
        QVERIFY(finished.wait(20000));
        const auto results = finished.first().at(0).value<QVector<SyncResult>>();
        QCOMPARE(results.size(), 2);
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("A")) + QStringLiteral("/a.txt")));
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("B")) + QStringLiteral("/b.txt")));
    }

    void unmountDeletesLocalFilesButNotDriveAndNotOtherFolders()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        const QString b = f.server.addFolder(QStringLiteral("B"));
        f.server.addFile(QStringLiteral("a.txt"), a, "in a");
        f.server.addFile(QStringLiteral("b.txt"), b, "in b");
        const QString idA = f.mount(a, QStringLiteral("A"));
        const QString idB = f.mount(b, QStringLiteral("B"));
        QCOMPARE(f.sync(idA).status, SyncResult::Success);
        QCOMPARE(f.sync(idB).status, SyncResult::Success);
        const QString stateA = f.core->folders()->stateFile(idA);
        QVERIFY(QFile::exists(stateA));

        QSignalSpy removed(f.core->folders(), &FolderManager::folderRemoved);
        QVERIFY(f.core->unmountFolder(idA));
        QVERIFY(removed.wait(10000));
        QCOMPARE(removed.first().at(1).toBool(), true);

        // local copy of A is gone ...
        QVERIFY(!QFileInfo::exists(f.localDir(QStringLiteral("A"))));
        QVERIFY(!QFile::exists(stateA));
        QVERIFY(!f.places.places.contains(f.localDir(QStringLiteral("A"))));
        // ... Drive is untouched ...
        QVERIFY(f.server.existsPath(QStringLiteral("A/a.txt")));
        QVERIFY(!f.server.file(a).trashed);
        // ... and B is not affected
        QCOMPARE(f.core->folders()->folders().size(), 1);
        QCOMPARE(readFile(f.localDir(QStringLiteral("B")) + QStringLiteral("/b.txt")), QByteArray("in b"));
        QVERIFY(f.places.places.contains(f.localDir(QStringLiteral("B"))));
        QCOMPARE(f.sync(idB).status, SyncResult::Success);

        // the same Drive folder can be mounted again in the same login session
        const QString again = f.mount(a, QStringLiteral("A"));
        QVERIFY(!again.isEmpty());
        QCOMPARE(f.sync(again).downloaded, 1);
    }

    void unmountIsRefusedWhileSyncing()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        f.server.addFile(QStringLiteral("big.bin"), a, QByteArray(2000000, 'x'));
        f.server.setDownloadChunkDelayMs(10);
        const QString id = f.mount(a, QStringLiteral("A"));
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(id));
        QString error;
        QVERIFY(!f.core->unmountFolder(id, &error));
        QVERIFY(!error.isEmpty());
        f.core->cancelSync();
        QVERIFY(finished.count() > 0 || finished.wait(10000));
        QVERIFY(f.core->unmountFolder(id));
    }

    void foldersSurviveRestart()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        f.server.addFile(QStringLiteral("a.txt"), a, "in a");
        const QString id = f.mount(a, QStringLiteral("A"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);

        f.createCore();
        QVERIFY(f.initialize());
        QCOMPARE(f.core->folders()->folders().size(), 1);
        QCOMPARE(f.core->folders()->folders().first().id, id);
        QVERIFY(f.core->folders()->folders().first().lastSync.isValid());
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.downloaded, 0); // state was kept: nothing to do
    }
};

QTEST_GUILESS_MAIN(TestFolders)
#include "tst_folders.moc"
