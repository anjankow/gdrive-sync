// Requirement: all events are logged with their severity to a log file whose
// location is configurable (below /tmp by default):
//   INFO  sign-in/sign-out and configuration changes
//   DEBUG file syncs
//   WARN  sync errors
//   ERROR unexpected application-level errors
#include "logger.h"
#include "testutil.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>

using namespace gds;
using namespace gdstest;

namespace {
QStringList g_console; // what reached the console in libraryOutputStaysOffTheConsole()
void captureConsole(QtMsgType, const QMessageLogContext&, const QString& message)
{
    g_console.append(message);
}

bool anyContains(const QStringList& lines, const QString& part)
{
    return std::any_of(lines.cbegin(), lines.cend(), [&](const QString& l) { return l.contains(part); });
}
}

class TestLogging : public QObject {
    Q_OBJECT

    static bool login(Fixture& f)
    {
        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        f.core->login();
        return ok.wait(10000);
    }

private Q_SLOTS:
    void lineFormatHasTimestampAndSeverity()
    {
        const QDateTime time(QDate(2026, 1, 31), QTime(12, 0, 0, 5), Qt::OffsetFromUTC, 3600);
        QCOMPARE(Logger::formatLine(LogLevel::Info, QStringLiteral("hello"), time),
            QStringLiteral("2026-01-31T12:00:00.005+01:00 INFO  hello\n"));
        QCOMPARE(Logger::formatLine(LogLevel::Debug, QStringLiteral("a\nb"), time),
            QStringLiteral("2026-01-31T12:00:00.005+01:00 DEBUG a\\nb\n")); // always one line per event
        QVERIFY(Logger::formatLine(LogLevel::Warn, QStringLiteral("x"), time)
                    .contains(QStringLiteral(" WARN  x")));
        QVERIFY(Logger::formatLine(LogLevel::Error, QStringLiteral("x"), time)
                    .contains(QStringLiteral(" ERROR x")));
    }

    void defaultLocationIsBelowTheTempDirectory()
    {
        QVERIFY(Logger::defaultDirectory().startsWith(QDir::tempPath() + QStringLiteral("/gdrive-sync-")));
        QCOMPARE(AppPaths::defaults().logDir, Logger::defaultDirectory());
    }

    void logFolderCreatedByTheApplicationIsPrivate()
    {
        Fixture f;
        f.createCore();
        const QString file = f.core->logFilePath();
        QCOMPARE(file, f.paths().logDir + QStringLiteral("/gdrive-sync.log"));
        const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QVERIFY(!(QFile::permissions(f.paths().logDir) & others));
        QVERIFY(!(QFile::permissions(file) & others));
    }

    void signInSignOutAndConfigurationChangesAreInfo()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("cid"), QStringLiteral("very-secret-value")));
        f.server.setAccountEmail(QStringLiteral("alice@example.com"));
        QVERIFY(login(f));
        f.core->setDefaultDirection(SyncDirection::DriveToLocal);
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QVERIFY(f.core->setFolderDirection(id, SyncDirection::LocalToDrive));
        QSignalSpy autoSync(f.core.get(), &AppCore::autoSyncChanged);
        f.core->setAutoSync(true, 45);
        QVERIFY(autoSync.wait(10000));
        f.core->setAutoSync(false, 0);
        QVERIFY(autoSync.wait(10000));
        QSignalSpy removed(f.core->folders(), &FolderManager::folderRemoved);
        QVERIFY(f.core->unmountFolder(id));
        QVERIFY(removed.wait(10000));
        f.core->logout();

        const QStringList info = f.logLines(QStringLiteral("INFO"));
        QVERIFY(anyContains(info, QStringLiteral("Google API client changed")));
        QVERIFY(anyContains(info, QStringLiteral("Sign-in started")));
        QVERIFY(anyContains(info, QStringLiteral("Signed in as alice@example.com")));
        QVERIFY(anyContains(
            info, QStringLiteral("Default sync direction for new folders set to Drive to local")));
        QVERIFY(anyContains(info, QStringLiteral("set to Local to Drive")));
        QVERIFY(anyContains(info, QStringLiteral("Folder added: Drive folder \"Docs\"")));
        QVERIFY(anyContains(info, QStringLiteral("Automatic sync enabled, every 45 minutes")));
        QVERIFY(anyContains(info, QStringLiteral("Automatic sync disabled")));
        QVERIFY(anyContains(info, QStringLiteral("Folder unmounted: \"Docs\"")));
        QVERIFY(anyContains(info, QStringLiteral("Signed out of alice@example.com")));
        QVERIFY(f.logLines(QStringLiteral("WARN")).isEmpty());
        QVERIFY(f.logLines(QStringLiteral("ERROR")).isEmpty());

        // secrets never reach the log
        const QString text = f.logText();
        QVERIFY(!text.contains(QStringLiteral("very-secret-value")));
        for (const QString& token : f.server.issuedRefreshTokens())
            QVERIFY(!text.contains(token));
        QVERIFY(!text.contains(QStringLiteral("at-id")));
    }

    void fileSyncsAreDebug()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString gone = f.server.addFile(QStringLiteral("gone.txt"), docs, "x");
        f.server.addFile(QStringLiteral("down.txt"), docs, "12345");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const QString dir = f.localDir(QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QVERIFY(writeFile(dir + QStringLiteral("/sub/up.txt"), "abc"));
        QVERIFY(QFile::remove(dir + QStringLiteral("/down.txt")));
        f.server.setTrashed(gone);
        QCOMPARE(f.sync(id).status, SyncResult::Success);

        const QStringList debug = f.logLines(QStringLiteral("DEBUG"));
        QVERIFY(anyContains(debug, QStringLiteral("Sync started: \"Docs\"")));
        QVERIFY(anyContains(debug, QStringLiteral("Downloaded %1/down.txt (5 bytes)").arg(dir)));
        QVERIFY(anyContains(debug, QStringLiteral("Downloaded %1/gone.txt (1 bytes)").arg(dir)));
        QVERIFY(anyContains(debug, QStringLiteral("Created Drive folder sub")));
        QVERIFY(anyContains(debug, QStringLiteral("Uploaded %1/sub/up.txt (3 bytes)").arg(dir)));
        QVERIFY(anyContains(debug, QStringLiteral("confirm 1 local and 1 Drive deletion(s)")));
        QVERIFY(anyContains(debug, QStringLiteral("Deletions confirmed")));
        QVERIFY(anyContains(debug, QStringLiteral("Deleted locally %1/gone.txt").arg(dir)));
        QVERIFY(anyContains(debug, QStringLiteral("Moved to the Drive trash: down.txt")));
        QVERIFY(anyContains(debug, QStringLiteral("Sync finished: \"Docs\"")));
        // syncing is not logged as INFO, and a clean sync produces no warnings
        QVERIFY(!anyContains(f.logLines(QStringLiteral("INFO")), QStringLiteral("Downloaded")));
        QVERIFY(!anyContains(f.logLines(QStringLiteral("INFO")), QStringLiteral("Sync started")));
        QVERIFY(f.logLines(QStringLiteral("WARN")).isEmpty());
    }

    void syncErrorsAreWarnings()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString bad = f.server.addFile(QStringLiteral("bad.txt"), docs, "x");
        f.server.addFile(QStringLiteral("good.txt"), docs, "y");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        f.server.failNext(QStringLiteral("/drive/v3/files/") + bad, 404, 1);
        QCOMPARE(f.sync(id).status, SyncResult::Failed);

        const QStringList warn = f.logLines(QStringLiteral("WARN"));
        QVERIFY(anyContains(warn, QStringLiteral("Sync error in \"Docs\"")));
        QVERIFY(anyContains(warn, QStringLiteral("bad.txt")));
        QVERIFY(anyContains(warn, QStringLiteral("Sync finished: \"Docs\"")));
        QVERIFY(anyContains(f.logLines(QStringLiteral("DEBUG")), QStringLiteral("good.txt")));
        QVERIFY(f.logLines(QStringLiteral("ERROR")).isEmpty()); // a failed file is not an application error
    }

    void failedSignInAndLostAuthorizationAreWarnings()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("cid"), QString()));
        f.server.setDenyAuthorization(true);
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        QVERIFY(failed.wait(10000));
        QVERIFY(anyContains(f.logLines(QStringLiteral("WARN")), QStringLiteral("Sign-in failed")));

        Fixture g;
        QVERIFY(g.startLoggedIn());
        const QString id = g.mount(g.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        g.server.invalidateRefreshTokens();
        QCOMPARE(g.sync(id).status, SyncResult::AuthRequired);
        QVERIFY(anyContains(
            g.logLines(QStringLiteral("WARN")), QStringLiteral("Google rejected the stored sign-in")));
    }

    void unexpectedApplicationErrorsAreErrors()
    {
        {
            Fixture f; // the wallet cannot be opened when a sync needs it
            f.secrets.failOpen = true;
            QVERIFY(f.startLoggedIn());
            const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
            QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
            QVERIFY(f.core->syncFolder(id));
            QVERIFY(finished.wait(5000));
            QVERIFY(anyContains(
                f.logLines(QStringLiteral("ERROR")), QStringLiteral("KDE wallet could not be opened")));
        }
        {
            Fixture f; // systemd refuses the timer
            QVERIFY(f.startLoggedIn());
            f.systemctlFails = true;
            QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
            f.core->setAutoSync(true, 30);
            QVERIFY(changed.wait(10000));
            QVERIFY(anyContains(
                f.logLines(QStringLiteral("ERROR")), QStringLiteral("Automatic sync could not be changed")));
        }
    }

    void failedWalletWriteIsAnErrorWithReasonAndOutcome()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("old-id"), QStringLiteral("old-secret")));

        // the wallet gets closed behind the application's back
        f.secrets.failWrites = true;
        f.secrets.failReason
            = QStringLiteral("the KDE wallet was closed or locked after the application opened it");
        QString error;
        QVERIFY(!f.setClient(QStringLiteral("new-id"), QStringLiteral("new-secret"), &error));
        // what happened, why, and what the outcome is
        QVERIFY(error.contains(QStringLiteral("could not be stored in the KDE wallet")));
        QVERIFY(error.contains(QStringLiteral("the KDE wallet was closed or locked")));
        QVERIFY(error.contains(QStringLiteral("The change was aborted")));
        QVERIFY(error.contains(QStringLiteral("the previously saved client stays in effect")));
        QCOMPARE(f.core->clientId(), QStringLiteral("old-id"));
        QCOMPARE(f.core->clientSecret(), QStringLiteral("old-secret"));
        const QStringList errors = f.logLines(QStringLiteral("ERROR"));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.first(), error);
        QVERIFY(!f.logText().contains(QStringLiteral("new-secret")));
    }

    void walletFailureHalfwayLeavesThePreviousClient()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("old-id"), QStringLiteral("old-secret")));
        f.secrets.failWriteOfKey = SecretKeys::clientSecret(); // the ID is written, the secret is not
        QString error;
        QVERIFY(!f.setClient(QStringLiteral("new-id"), QStringLiteral("new-secret"), &error));
        QCOMPARE(f.core->clientId(), QStringLiteral("old-id")); // no new ID with the old secret
        QCOMPARE(f.core->clientSecret(), QStringLiteral("old-secret"));
    }

    void firstClientThatCannotBeStoredKeepsSignInUnavailable()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        f.secrets.failWrites = true;
        QString error;
        QVERIFY(!f.setClient(QStringLiteral("id"), QStringLiteral("secret"), &error));
        QVERIFY(error.contains(QStringLiteral("signing in stays unavailable until the client is saved")));
        QVERIFY(!f.core->hasClientCredentials());
    }

    void signInThatCannotBeStoredIsAbortedWithAnError()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("cid"), QString()));
        f.secrets.failWriteOfKey = SecretKeys::refreshToken();
        f.secrets.failReason = QStringLiteral("the KDE wallet was closed");
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        QVERIFY(failed.wait(10000));
        const QString message = failed.first().at(0).toString();
        QVERIFY(message.contains(QStringLiteral("the KDE wallet was closed")));
        QVERIFY(message.contains(QStringLiteral("you are not signed in")));
        QVERIFY(!f.core->isLoggedIn());
        QVERIFY(anyContains(
            f.logLines(QStringLiteral("ERROR")), QStringLiteral("sign-in data could not be stored")));
    }

    void everyLineCarriesASeverity()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "a");
        QCOMPARE(f.sync(f.mount(docs, QStringLiteral("Docs"))).status, SyncResult::Success);
        const QStringList lines = f.logText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(lines.size() >= 5);
        const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}[+-]"
                                                   "\\d{2}:\\d{2} (DEBUG|INFO |WARN |ERROR) \\S.*$"));
        for (const QString& line : lines)
            QVERIFY2(re.match(line).hasMatch(), qPrintable(line));
    }

    void logLocationCanBeChangedAndIsRemembered()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString first = f.core->logFilePath();
        const QString chosen = f.tmp.path() + QStringLiteral("/elsewhere/logs");
        QString error;
        QVERIFY2(f.core->setLogDirectory(chosen, &error), qPrintable(error));
        QCOMPARE(f.core->logDirectory(), chosen);
        QCOMPARE(f.core->logFilePath(), chosen + QStringLiteral("/gdrive-sync.log"));
        // the change itself is a configuration change: INFO in the new file
        QVERIFY(anyContains(f.logLines(QStringLiteral("INFO")), QStringLiteral("Log location changed to")));

        // later events go to the new file only
        const QByteArray oldContent = readFile(first);
        f.core->setDefaultDirection(SyncDirection::LocalToDrive);
        QVERIFY(f.logText().contains(QStringLiteral("new folders set to Local to Drive")));
        QCOMPARE(readFile(first), oldContent);

        // remembered after a restart
        f.createCore();
        QVERIFY(f.initialize());
        QCOMPARE(f.core->logDirectory(), chosen);
        QCOMPARE(f.core->logFilePath(), chosen + QStringLiteral("/gdrive-sync.log"));

        // an empty choice selects the default again
        QVERIFY(f.core->setLogDirectory(QString()));
        QCOMPARE(f.core->logDirectory(), f.paths().logDir);
    }

    void unusableLogLocationIsRefusedAndTheOldOneKept()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString before = f.core->logFilePath();
        QVERIFY(writeFile(f.tmp.path() + QStringLiteral("/a-file"), "x"));
        QString error;
        QVERIFY(!f.core->setLogDirectory(f.tmp.path() + QStringLiteral("/a-file/sub"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!f.core->setLogDirectory(QStringLiteral("relative/path"), &error));
        QCOMPARE(f.core->logFilePath(), before);
        QCOMPARE(f.core->logDirectory(), f.paths().logDir);
        QVERIFY(anyContains(f.logLines(QStringLiteral("ERROR")), QStringLiteral("cannot be used")));
    }

    void oldestEntriesAreDroppedAtOneMegabyte()
    {
        QCOMPARE(Logger::maxBytes(), qint64(1024) * 1024);
        Fixture f;
        f.createCore();
        const QString file = f.core->logFilePath();
        const QString padding(150, QLatin1Char('x'));
        qint64 largest = 0;
        const int count = 12000; // about 2.4 MB of entries
        for (int i = 0; i < count; ++i) {
            logDebug(QStringLiteral("entry %1 %2").arg(i, 6, 10, QLatin1Char('0')).arg(padding));
            if (i % 200 == 0)
                largest = qMax(largest, QFileInfo(file).size());
        }
        largest = qMax(largest, QFileInfo(file).size());
        // the file never exceeds the limit ...
        QVERIFY2(largest <= Logger::maxBytes(), qPrintable(QString::number(largest)));
        QVERIFY(largest > Logger::maxBytes() / 2);
        // ... there is only this one file ...
        QCOMPARE(
            QDir(f.paths().logDir).entryList(QDir::Files), QStringList {QStringLiteral("gdrive-sync.log")});

        const QStringList lines = f.logText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        // ... the newest entries are there, the oldest are gone ...
        QVERIFY(lines.last().contains(QStringLiteral("entry %1").arg(count - 1, 6, 10, QLatin1Char('0'))));
        QVERIFY(!f.logText().contains(QStringLiteral("entry 000000 ")));
        QVERIFY(!f.logText().contains(QStringLiteral("entry 000100 ")));
        // ... and what remains is whole, consecutive entries in order
        const QRegularExpression re(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\S+ DEBUG entry (\\d{6}) x+$"));
        int previous = -1;
        for (const QString& line : lines) {
            const auto m = re.match(line);
            QVERIFY2(m.hasMatch(), qPrintable(line.left(80)));
            const int n = m.captured(1).toInt();
            if (previous >= 0)
                QCOMPARE(n, previous + 1);
            previous = n;
        }
        QCOMPARE(previous, count - 1);
    }

    void oversizedExistingLogIsTrimmedWhenOpened()
    {
        Fixture f;
        const QString file = f.paths().logDir + QStringLiteral("/gdrive-sync.log");
        QByteArray old;
        for (int i = 0; old.size() < 3 * Logger::maxBytes(); ++i)
            old += "2026-01-01T00:00:00.000+00:00 INFO  old entry " + QByteArray::number(i) + '\n';
        QVERIFY(writeFile(file, old));
        f.createCore();
        QVERIFY(QFileInfo(file).size() <= Logger::maxBytes());
        const QString text = f.logText();
        QVERIFY(text.startsWith(QStringLiteral("2026-01-01T00:00:00.000+00:00 INFO  old entry ")));
        QVERIFY(!text.contains(QStringLiteral("old entry 0\n")));
    }

    void logIsAppendedAcrossRestarts()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        f.core->setDefaultDirection(SyncDirection::DriveToLocal);
        const int before = f.logText().count(QLatin1Char('\n'));
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.logText().contains(QStringLiteral("new folders set to Drive to local")));
        QVERIFY(f.logText().count(QLatin1Char('\n')) > before);
    }

    void libraryOutputStaysOffTheConsole()
    {
        Fixture f;
        f.createCore(); // opens the log file
        g_console.clear();
        const QtMessageHandler qtest = installQtMessageRouting(captureConsole);
        // what plugins loaded into the process print, e.g. kio-admin's file item action
        qDebug() << "actions!!!";
        qDebug() << "urly!!!" << QUrl(QStringLiteral("admin:/tmp/x/"));
        const QLoggingCategory konsole("org.kde.konsole.keytranslator");
        qCWarning(konsole) << "Line in keyboard translator file could not be parsed";
        qInfo("some info");
        const QLoggingCategory broken("kf.broken");
        qCCritical(broken) << "something really failed";
        qInstallMessageHandler(qtest);

        // only the serious message reaches the console
        QCOMPARE(g_console.size(), 1);
        QVERIFY(g_console.first().contains(QStringLiteral("something really failed")));
        // everything is in the log file, the rest at DEBUG level
        QVERIFY(anyContains(f.logLines(QStringLiteral("ERROR")), QStringLiteral("something really failed")));
        const QStringList debug = f.logLines(QStringLiteral("DEBUG"));
        QVERIFY(anyContains(debug, QStringLiteral("Library message (default): actions!!!")));
        QVERIFY(anyContains(debug, QStringLiteral("urly!!!")));
        QVERIFY(anyContains(debug, QStringLiteral("Library message (org.kde.konsole.keytranslator)")));
        QVERIFY(anyContains(debug, QStringLiteral("some info")));
    }
};

QTEST_GUILESS_MAIN(TestLogging)
#include "tst_logging.moc"
