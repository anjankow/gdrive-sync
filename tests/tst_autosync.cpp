// Requirement: "There should be additional option to configure automatic sync
// running as a cronjob" -> systemd user timer, installed only on request,
// running an unattended sync of all folders.
#include "scheduler.h"
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestAutoSync : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void unitsAreWellFormed()
    {
        const QString service = Scheduler::serviceUnit(QStringLiteral("/opt/my app/gdrive-sync"));
        QVERIFY(service.contains(QStringLiteral("[Service]\nType=oneshot\n")));
        QVERIFY(service.contains(QStringLiteral("ExecStart=\"/opt/my app/gdrive-sync\" --sync\n")));

        const QString timer = Scheduler::timerUnit(45);
        QVERIFY(timer.contains(QStringLiteral("[Timer]\n")));
        QVERIFY(timer.contains(QStringLiteral("OnUnitActiveSec=45min\n")));
        QVERIFY(timer.contains(QStringLiteral("Unit=gdrive-sync.service\n")));
        QVERIFY(timer.contains(QStringLiteral("[Install]\nWantedBy=timers.target\n")));
        QVERIFY(Scheduler::timerUnit(0).contains(QStringLiteral("OnUnitActiveSec=1min")));
    }

    void nothingIsInstalledUntilRequested()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QVERIFY(!f.core->autoSyncEnabled());
        QCOMPARE(f.core->autoSyncIntervalMinutes(), 0);
        QVERIFY(f.systemctlCalls.isEmpty());
        QVERIFY(!QFileInfo::exists(f.paths().unitDir));
    }

    void enablingInstallsAndStartsTheTimer()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        f.core->setAutoSync(true, 30);
        QVERIFY(changed.wait(10000));
        QCOMPARE(changed.first().at(0).toBool(), true);
        QVERIFY(f.core->autoSyncEnabled());
        QCOMPARE(f.core->autoSyncIntervalMinutes(), 30);

        const QString dir = f.paths().unitDir;
        QCOMPARE(QString::fromUtf8(readFile(dir + QStringLiteral("/gdrive-sync.service"))),
            Scheduler::serviceUnit(QStringLiteral("/usr/bin/gdrive-sync")));
        QCOMPARE(QString::fromUtf8(readFile(dir + QStringLiteral("/gdrive-sync.timer"))),
            Scheduler::timerUnit(30));
        QCOMPARE(f.systemctlCalls,
            (QStringList {QStringLiteral("daemon-reload"), QStringLiteral("enable --now gdrive-sync.timer"),
                QStringLiteral("restart gdrive-sync.timer")}));

        // changing the interval rewrites the timer
        changed.clear();
        f.core->setAutoSync(true, 120);
        QVERIFY(changed.wait(10000));
        QCOMPARE(f.core->autoSyncIntervalMinutes(), 120);
    }

    void disablingStopsAndRemovesTheTimer()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        f.core->setAutoSync(true, 30);
        QVERIFY(changed.wait(10000));
        f.systemctlCalls.clear();
        changed.clear();
        f.core->setAutoSync(false, 0);
        QVERIFY(changed.wait(10000));
        QVERIFY(!f.core->autoSyncEnabled());
        QVERIFY(!QFile::exists(f.paths().unitDir + QStringLiteral("/gdrive-sync.timer")));
        QVERIFY(!QFile::exists(f.paths().unitDir + QStringLiteral("/gdrive-sync.service")));
        QCOMPARE(f.systemctlCalls,
            (QStringList {
                QStringLiteral("disable --now gdrive-sync.timer"), QStringLiteral("daemon-reload")}));
    }

    void failedInstallationLeavesNothingBehind()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.systemctlFails = true;
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        f.core->setAutoSync(true, 30);
        QVERIFY(changed.wait(10000));
        QCOMPARE(changed.first().at(0).toBool(), false);
        QVERIFY(!changed.first().at(1).toString().isEmpty());
        QVERIFY(!f.core->autoSyncEnabled());
    }

    void logoutRemovesTheTimer()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        f.core->setAutoSync(true, 30);
        QVERIFY(changed.wait(10000));
        changed.clear();
        f.core->logout();
        QVERIFY(changed.wait(10000));
        QVERIFY(!f.core->autoSyncEnabled());
        QVERIFY(f.systemctlCalls.contains(QStringLiteral("disable --now gdrive-sync.timer")));
    }

    void automaticRunSyncsAllFoldersUnattended()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        const QString b = f.server.addFolder(QStringLiteral("B"));
        f.server.addFile(QStringLiteral("a.txt"), a, "in a");
        f.server.addFile(QStringLiteral("b.txt"), b, "in b");
        f.mount(a, QStringLiteral("A"));
        f.mount(b, QStringLiteral("B"));

        // what "gdrive-sync --sync" does: a new process restores the login and syncs everything
        f.createCore();
        QVERIFY(f.initialize());
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncAll(/*interactive=*/false));
        QVERIFY(finished.wait(20000));
        const auto results = finished.first().at(0).value<QVector<SyncResult>>();
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.at(0).status, SyncResult::Success);
        QCOMPARE(results.at(1).status, SyncResult::Success);
        QCOMPARE(readFile(f.localDir(QStringLiteral("A")) + QStringLiteral("/a.txt")), QByteArray("in a"));
        QCOMPARE(readFile(f.localDir(QStringLiteral("B")) + QStringLiteral("/b.txt")), QByteArray("in b"));
        QCOMPARE(f.deletionQuestions, 0);
        QVERIFY(f.notifier.events.isEmpty()); // nothing needs attention
    }

    void automaticRunNotifiesAboutFailures()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.server.addFolder(QStringLiteral("A"));
        f.mount(a, QStringLiteral("A"));
        f.server.invalidateRefreshTokens();
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncAll(false));
        QVERIFY(finished.wait(20000));
        QCOMPARE(f.notifier.events, QStringList {QStringLiteral("syncFailed")});
    }
};

QTEST_GUILESS_MAIN(TestAutoSync)
#include "tst_autosync.moc"
