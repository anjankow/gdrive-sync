// Requirement: "The mounted drive appears in file explorer such as dolphin."
// Dolphin's Places panel is backed by KFilePlacesModel (user-places.xbel).
#include "kdeintegration.h"
#include "testutil.h"

#include <KFilePlacesModel>

#include <QStandardPaths>

using namespace gds;
using namespace gdstest;

class TestPlaces : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

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
