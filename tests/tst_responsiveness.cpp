// Requirement: "UI should be always responsive, independently of the running
// sync or other running processes."
//
// A heartbeat timer in the GUI thread measures how long the event loop was
// ever blocked while slow operations are running in the background.
#include "dialogs.h"
#include "mainwindow.h"
#include "testutil.h"

#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>

using namespace gds;
using namespace gdstest;

namespace {

/// Largest pause between two event-loop iterations of the GUI thread.
class Heartbeat : public QObject {
public:
    Heartbeat()
    {
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_maxGap = qMax(m_maxGap, m_clock.restart());
            ++m_beats;
        });
        m_timer.setTimerType(Qt::PreciseTimer);
        m_clock.start();
        m_timer.start(5);
    }
    qint64 maxGapMs() const { return m_maxGap; }
    int beats() const { return m_beats; }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_maxGap = 0;
    int m_beats = 0;
};

// Generous for loaded CI machines, yet far below the seconds the operations take.
const qint64 kMaxBlockMs = 250;

}

class TestResponsiveness : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void guiStaysResponsiveDuringASlowSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        for (int i = 0; i < 4; ++i)
            f.server.addFile(QStringLiteral("big%1.bin").arg(i), docs, QByteArray(700000, char('a' + i)));
        f.server.setDownloadChunkDelayMs(8);
        f.server.setResponseDelayMs(150); // every request to "Google" is slow
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        // files to hash and upload as well
        for (int i = 0; i < 3; ++i)
            QVERIFY(writeFile(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/up%1.bin").arg(i),
                QByteArray(3000000, 'u')));
        window.selectFolder(id);

        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QElapsedTimer total;
        total.start();
        Heartbeat heartbeat;
        QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("syncButton")), Qt::LeftButton);
        QVERIFY(finished.wait(60000));
        const qint64 duration = total.elapsed();
        QCOMPARE(finished.first().at(0).value<QVector<SyncResult>>().first().status, SyncResult::Success);

        QVERIFY2(duration > 1500, "the sync was meant to be slow");
        QVERIFY2(heartbeat.maxGapMs() < kMaxBlockMs,
            qPrintable(QStringLiteral("GUI thread was blocked for %1 ms").arg(heartbeat.maxGapMs())));
        // the event loop kept spinning the whole time
        QVERIFY(heartbeat.beats() > duration / 20);
    }

    void cancelButtonWorksImmediatelyDuringSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("huge.bin"), docs, QByteArray(8000000, 'h'));
        f.server.setDownloadChunkDelayMs(20); // would take about 10 seconds
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        window.selectFolder(id);
        auto* cancel = window.findChild<QPushButton*>(QStringLiteral("cancelButton"));
        auto* bar = window.findChild<QProgressBar*>(QStringLiteral("progressBar"));
        QVERIFY(!cancel->isEnabled());

        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("syncButton")), Qt::LeftButton);
        QTRY_VERIFY(bar->value() > 0); // the window keeps repainting progress
        QVERIFY(cancel->isEnabled());
        QElapsedTimer timer;
        timer.start();
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(finished.wait(5000));
        QVERIFY2(timer.elapsed() < 1500, "cancel must take effect quickly");
        QCOMPARE(finished.first().at(0).value<QVector<SyncResult>>().first().status, SyncResult::Cancelled);
        QVERIFY(!cancel->isEnabled());
    }

    void folderBrowserDoesNotBlockWhileDriveIsSlow()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.server.addFolder(QStringLiteral("Docs"));
        f.server.setResponseDelayMs(1200);
        // Building and first showing the dialog is GUI work of its own (slow under
        // sanitizers), so it is only required not to wait for the Drive answer.
        QElapsedTimer opening;
        opening.start();
        AddFolderDialog dialog(f.core.get());
        dialog.show();
        QVERIFY2(opening.elapsed() < 1000, "opening the dialog must not wait for Drive");
        Heartbeat heartbeat;
        QTreeWidgetItem* root = dialog.tree()->topLevelItem(0);
        // the dialog is usable at once and shows a placeholder while loading
        QCOMPARE(root->childCount(), 1);
        QCOMPARE(root->child(0)->text(0), QStringLiteral("Loading…"));
        dialog.setLocalPath(f.tmp.path() + QStringLiteral("/typed-while-loading"));
        QTRY_VERIFY_WITH_TIMEOUT(
            root->childCount() == 1 && root->child(0)->text(0) == QLatin1String("Docs"), 15000);
        QVERIFY2(heartbeat.maxGapMs() < kMaxBlockMs,
            qPrintable(QStringLiteral("GUI thread was blocked for %1 ms").arg(heartbeat.maxGapMs())));
    }

    void waitingForTheUsersAnswerDoesNotBlockTheGui()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QObject::disconnect(f.core.get(), &AppCore::deletionsPending, nullptr, nullptr);
        MainWindow window(f.core.get());
        window.show();
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString gone = f.server.addFile(QStringLiteral("gone.txt"), docs, "x");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        window.selectFolder(id);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("syncButton")), Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        f.server.setTrashed(gone);
        finished.clear();
        QTest::mouseClick(window.findChild<QPushButton*>(QStringLiteral("syncButton")), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<DeletionDialog*>());

        // the sync engine now waits for the answer; the GUI must keep running
        Heartbeat heartbeat;
        QTest::qWait(700);
        QVERIFY(f.core->isSyncing());
        QVERIFY(heartbeat.maxGapMs() < kMaxBlockMs);
        QVERIFY(heartbeat.beats() > 30);
        window.findChild<DeletionDialog*>()->reject();
        QVERIFY(finished.wait(10000));
    }

    void unmountingALargeFolderDoesNotBlock()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        for (int d = 0; d < 40; ++d) {
            for (int i = 0; i < 50; ++i)
                QVERIFY(writeFile(
                    f.localDir(QStringLiteral("Docs")) + QStringLiteral("/d%1/f%2.txt").arg(d).arg(i), "x"));
        }
        QSignalSpy removed(f.core->folders(), &FolderManager::folderRemoved);
        Heartbeat heartbeat;
        QVERIFY(f.core->unmountFolder(id));
        QVERIFY(removed.wait(30000));
        QVERIFY(!QFileInfo::exists(f.localDir(QStringLiteral("Docs"))));
        QVERIFY2(heartbeat.maxGapMs() < kMaxBlockMs,
            qPrintable(QStringLiteral("GUI thread was blocked for %1 ms").arg(heartbeat.maxGapMs())));
    }

    void slowSystemctlDoesNotBlock()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        // replace the systemctl runner by one that takes long (like a busy system)
        f.core.reset();
        f.core = std::make_unique<AppCore>(
            GoogleEndpoints::forTestServer(f.server.baseUrl()), &f.secrets, &f.places, &f.notifier, f.paths(),
            [](const QUrl&) { return false; },
            [](const QStringList&, QString*) {
                QThread::msleep(400);
                return true;
            });
        QVERIFY(f.initialize());
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        Heartbeat heartbeat;
        f.core->setAutoSync(true, 30);
        QVERIFY(changed.wait(15000));
        QVERIFY(f.core->autoSyncEnabled());
        QVERIFY2(heartbeat.maxGapMs() < kMaxBlockMs,
            qPrintable(QStringLiteral("GUI thread was blocked for %1 ms").arg(heartbeat.maxGapMs())));
    }

    void signInWaitsForTheBrowserWithoutBlocking()
    {
        Fixture f;
        f.secrets.open();
        QCoreApplication::processEvents();
        f.secrets.write(SecretKeys::clientId(), QStringLiteral("cid"));
        f.server.setResponseDelayMs(600); // slow token endpoint
        f.createCore();
        QVERIFY(f.initialize());
        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        Heartbeat heartbeat;
        f.core->login();
        QVERIFY(f.core->isLoginInProgress());
        QVERIFY(ok.wait(20000));
        QVERIFY2(heartbeat.maxGapMs() < kMaxBlockMs,
            qPrintable(QStringLiteral("GUI thread was blocked for %1 ms").arg(heartbeat.maxGapMs())));
    }
};

QTEST_MAIN(TestResponsiveness)
#include "tst_responsiveness.moc"
