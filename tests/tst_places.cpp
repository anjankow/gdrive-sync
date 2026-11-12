// Requirement: "The mounted drive appears in file explorer such as dolphin."
// Dolphin's Places panel is backed by KFilePlacesModel (user-places.xbel).
#include "hostenvironment.h"
#include "kdeintegration.h"
#include "testutil.h"

#include <KFilePlacesModel>

#include <QDir>
#include <QStandardPaths>

using namespace gds;
using namespace gdstest;

class TestPlaces : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // Dolphin opened by KDE on /tmp/x/folder runs in /tmp/x. After an unmount (or
    // the user) deletes that directory, everything it starts prints fontconfig's
    // "out of memory". The file manager is therefore started from home.
    void fileManagerStartsFromTheHomeDirectoryAndNotFromTheFolder()
    {
        QTemporaryDir dir;
        const QString folder = dir.path() + QStringLiteral("/mounted/Drive Docs");
        QVERIFY(QDir().mkpath(folder));
        // a stand-in file manager that records where and with what it was started
        const QString script = dir.path() + QStringLiteral("/fake-file-manager");
        QVERIFY(writeFile(script,
            "#!/bin/sh\n"
            "pwd > \"$(dirname \"$0\")/cwd\"\n"
            "env > \"$(dirname \"$0\")/env\"\n"
            "printf '%s' \"$1\" > \"$(dirname \"$0\")/arg\"\n"
            "echo noise; echo noise >&2\n"));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString desktop = dir.path() + QStringLiteral("/fake-file-manager.desktop");
        QVERIFY(writeFile(desktop,
            "[Desktop Entry]\nType=Application\nName=Fake File Manager\nExec=" + script.toUtf8()
                + " %u\nMimeType=inode/directory;\n"));

        // as if run from an AppImage: the started program must not inherit its environment
        const QString appDir = dir.path() + QStringLiteral("/appimage-mount");
        qputenv("APPDIR", appDir.toUtf8());
        qputenv("APPIMAGE", "/somewhere/gdrive-sync.AppImage");
        qputenv("QT_PLUGIN_PATH", (appDir + QStringLiteral("/usr/plugins")).toUtf8());
        const QByteArray dataDirs = qgetenv("XDG_DATA_DIRS");
        qputenv(
            "XDG_DATA_DIRS", (appDir + QStringLiteral("/usr/share:/usr/local/share:/usr/share")).toUtf8());
        const bool started = ExternalPrograms::openFolder(folder, KService::Ptr(new KService(desktop)));
        qunsetenv("APPDIR");
        qunsetenv("APPIMAGE");
        qunsetenv("QT_PLUGIN_PATH");
        qputenv("XDG_DATA_DIRS", dataDirs);
        QVERIFY(started);
        QTRY_VERIFY(QFile::exists(dir.path() + QStringLiteral("/arg")));
        QTRY_VERIFY(!readFile(dir.path() + QStringLiteral("/cwd")).isEmpty());
        QCOMPARE(
            QString::fromUtf8(readFile(dir.path() + QStringLiteral("/cwd"))).trimmed(), QDir::homePath());
        QVERIFY(QString::fromUtf8(readFile(dir.path() + QStringLiteral("/arg"))).endsWith(folder));
        const QString env = QString::fromUtf8(readFile(dir.path() + QStringLiteral("/env")));
        QVERIFY2(!env.contains(appDir), qPrintable(env));
        QVERIFY(!env.contains(QStringLiteral("APPIMAGE=")));
        QVERIFY(env.contains(QStringLiteral("XDG_DATA_DIRS=/usr/local/share:/usr/share\n")));
    }

    void hostEnvironmentIsUnchangedOutsideAnAppImage()
    {
        QProcessEnvironment own;
        own.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
        own.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/opt/plugins"));
        QCOMPARE(hostEnvironment(own), own);

        own.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/.mount_abc"));
        own.insert(QStringLiteral("PATH"), QStringLiteral("/tmp/.mount_abc/usr/bin:/usr/bin:/bin"));
        own.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/tmp/.mount_abc/usr/plugins"));
        own.insert(
            QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/tmp/.mount_abcd/lib")); // only a similar name
        const QProcessEnvironment host = hostEnvironment(own);
        QCOMPARE(host.value(QStringLiteral("PATH")), QStringLiteral("/usr/bin:/bin"));
        QVERIFY(!host.contains(QStringLiteral("QT_PLUGIN_PATH")));
        QVERIFY(!host.contains(QStringLiteral("APPDIR")));
        QCOMPARE(host.value(QStringLiteral("LD_LIBRARY_PATH")), QStringLiteral("/tmp/.mount_abcd/lib"));
    }

    void mountedFolderAppearsInPlacesAndDisappearsOnUnmount()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/Drive Docs");
        QVERIFY(QDir().mkpath(path));
        KdePlaces places;
        QVERIFY(!places.hasPlace(path));
        places.addPlace(path, QStringLiteral("Docs (Google Drive)"));
        QVERIFY(places.hasPlace(path));

        // what Dolphin sees: an independent model instance reading the shared bookmarks
        {
            KFilePlacesModel dolphin;
            bool found = false;
            for (int row = 0; row < dolphin.rowCount(); ++row) {
                const QModelIndex index = dolphin.index(row, 0);
                if (dolphin.url(index) == QUrl::fromLocalFile(path)) {
                    found = true;
                    QCOMPARE(dolphin.text(index), QStringLiteral("Docs (Google Drive)"));
                    QVERIFY(!dolphin.isHidden(index));
                }
            }
            QVERIFY(found);
        }
        const QString xbel = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/user-places.xbel");
        QVERIFY(readFile(xbel).contains(QUrl::fromLocalFile(path).toEncoded()));

        places.addPlace(path, QStringLiteral("Docs (Google Drive)")); // no duplicate entry
        int count = 0;
        KFilePlacesModel check;
        for (int row = 0; row < check.rowCount(); ++row)
            count += check.url(check.index(row, 0)) == QUrl::fromLocalFile(path) ? 1 : 0;
        QCOMPARE(count, 1);

        places.removePlace(path);
        QVERIFY(!places.hasPlace(path));
        QVERIFY(!readFile(xbel).contains(QUrl::fromLocalFile(path).toEncoded()));
    }

    void mountingThroughTheApplicationAddsThePlace()
    {
        QTemporaryDir tmp;
        MockGoogleServer server;
        MemorySecretStore secrets;
        secrets.open();
        QCoreApplication::processEvents();
        KdePlaces places;
        AppPaths paths;
        paths.configFile = tmp.path() + QStringLiteral("/rc");
        paths.stateDir = tmp.path() + QStringLiteral("/state");
        paths.unitDir = tmp.path() + QStringLiteral("/units");
        paths.logDir = tmp.path() + QStringLiteral("/logs");
        AppCore core(
            GoogleEndpoints::forTestServer(server.baseUrl()), &secrets, &places, nullptr, paths,
            [](const QUrl&) { return false; }, [](const QStringList&, QString*) { return true; });
        const QString local = tmp.path() + QStringLiteral("/Docs");
        QString error;
        const QString id
            = core.folders()->addFolder(QStringLiteral("drive-id"), QStringLiteral("Docs"), local, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(places.hasPlace(local));

        QSignalSpy removed(core.folders(), &FolderManager::folderRemoved);
        QVERIFY(core.unmountFolder(id));
        QVERIFY(removed.wait(10000));
        QVERIFY(!places.hasPlace(local));
    }
};

QTEST_MAIN(TestPlaces)
#include "tst_places.moc"
