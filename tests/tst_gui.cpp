// GUI-level tests of the functional requirements: the widgets the user
// actually sees (folder selection, destination check, deletion warning,
// progress bar, sign-in/sign-out, automatic sync option).
#include "dialogs.h"
#include "mainwindow.h"
#include "testutil.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

using namespace gds;
using namespace gdstest;

namespace {
template <typename T> T* child(QWidget* parent, const char* name)
{
    return parent->findChild<T*>(QLatin1String(name));
}

QStringList items(QListWidget* list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        out.append(list->item(i)->text());
    return out;
}

/// Presses the button with the given standard role in the modal message box
/// that is about to open.
void answerNextMessageBox(QDialogButtonBox::StandardButton which)
{
    QTimer::singleShot(100, []() {});
    auto* timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, which]() {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal)
            return;
        auto* box = modal->findChild<QDialogButtonBox*>();
        if (!box || !box->button(which))
            return;
        timer->stop();
        timer->deleteLater();
        box->button(which)->click();
    });
    timer->start(20);
}
}

class TestGui : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ------------------------------------------------------------ deletions
    void deletionDialogListsEveryFileAndDefaultsToCancel()
    {
        DeletionDialog dialog(QStringLiteral("Docs"), {QStringLiteral("a.txt"), QStringLiteral("dir/")},
            {QStringLiteral("b.txt")});
        QCOMPARE(items(dialog.localList()), (QStringList {QStringLiteral("a.txt"), QStringLiteral("dir/")}));
        QCOMPARE(items(dialog.remoteList()), QStringList {QStringLiteral("b.txt")});
        QVERIFY(child<QLabel>(&dialog, "deletionIntro")->text().contains(QStringLiteral("3 items")));
        QVERIFY(child<QPushButton>(&dialog, "cancelDeletions")->isDefault());
        QVERIFY(!child<QPushButton>(&dialog, "confirmDeletions")->isDefault());

        QSignalSpy rejected(&dialog, &QDialog::rejected);
        dialog.show();
        dialog.reject(); // what closing the window or pressing Escape does
        QCOMPARE(rejected.count(), 1);
    }

    void syncShowsDeletionWarningAndCancelKeepsFiles()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        // the window answers the questions itself: drop the fixture's automatic answers
        QObject::disconnect(f.core.get(), &AppCore::deletionsPending, nullptr, nullptr);
        MainWindow window(f.core.get());
        window.show();

        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString gone = f.server.addFile(QStringLiteral("gone.txt"), docs, "x");
        f.server.addFile(QStringLiteral("stay.txt"), docs, "y");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        window.selectFolder(id);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        const QString localFile = f.localDir(QStringLiteral("Docs")) + QStringLiteral("/gone.txt");
        QVERIFY(QFile::exists(localFile));
        QVERIFY(!child<QDialog>(&window, "deletionDialog")); // nothing to delete: no warning

        // the file disappears from Drive: the next sync must warn before deleting it locally
        f.server.setTrashed(gone);
        finished.clear();
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QTRY_VERIFY(child<QDialog>(&window, "deletionDialog"));
        auto* dialog = child<DeletionDialog>(&window, "deletionDialog");
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(items(dialog->localList()), QStringList {QStringLiteral("gone.txt")});
        QVERIFY(QFile::exists(localFile)); // nothing deleted while the warning is shown
        QVERIFY(f.core->isSyncing());

        QTest::mouseClick(child<QPushButton>(dialog, "cancelDeletions"), Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.first().at(0).value<QVector<SyncResult>>().first().status, SyncResult::Cancelled);
        QVERIFY(QFile::exists(localFile));

        // confirming deletes it
        finished.clear();
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QTRY_VERIFY(child<DeletionDialog>(&window, "deletionDialog")
            && child<DeletionDialog>(&window, "deletionDialog")->isVisible());
        QTest::mouseClick(
            child<QPushButton>(child<DeletionDialog>(&window, "deletionDialog"), "confirmDeletions"),
            Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        QVERIFY(!QFile::exists(localFile));
        QVERIFY(QFile::exists(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/stay.txt")));
    }

    // ------------------------------------------------------------- progress
    void progressBarAndByteCountsAreShownDuringSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("big.bin"), docs, QByteArray(600000, 'x'));
        f.server.setDownloadChunkDelayMs(15);
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        window.selectFolder(id);

        auto* bar = child<QProgressBar>(&window, "progressBar");
        auto* text = child<QLabel>(&window, "progressText");
        QCOMPARE(bar->value(), 0);
        QVector<int> barValues;
        QStringList texts;
        // connected after the window's own slot: sees what the user sees
        QObject::connect(f.core.get(), &AppCore::progress, &window, [&](const SyncProgress&) {
            barValues.append(bar->value());
            texts.append(text->text());
        });
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QVERIFY(finished.wait(20000));

        QVERIFY(barValues.size() >= 4);
        QVERIFY(std::is_sorted(barValues.cbegin(), barValues.cend()));
        QVERIFY(std::any_of(barValues.cbegin(), barValues.cend(), [](int v) { return v > 0 && v < 1000; }));
        QCOMPARE(barValues.last(), 1000);
        QCOMPARE(bar->value(), 1000);
        // "<synced> of <total> synced, <remaining> remaining"
        const QRegularExpression re(
            QStringLiteral("^(.+) of 585\\.9 KiB synced, (.+) remaining \\((\\d) of 1 files\\)$"));
        bool sawPartial = false;
        for (const QString& t : qAsConst(texts)) {
            if (t.startsWith(QLatin1String("Comparing")))
                continue;
            const auto m = re.match(t);
            QVERIFY2(m.hasMatch(), qPrintable(t));
            if (m.captured(1) != QLatin1String("0 B") && m.captured(2) != QLatin1String("0 B"))
                sawPartial = true;
        }
        QVERIFY(sawPartial);
        QCOMPARE(texts.last(), QStringLiteral("585.9 KiB of 585.9 KiB synced, 0 B remaining (1 of 1 files)"));
    }

    // ------------------------------------------------ folder and destination
    void addFolderDialogSelectsADriveFolderAndRequiresEmptyDestination()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFolder(QStringLiteral("Photos"));
        const QString year = f.server.addFolder(QStringLiteral("2024"), docs);
        f.server.addFile(QStringLiteral("file.txt"), QStringLiteral("root"), "x");

        AddFolderDialog dialog(f.core.get());
        dialog.show();
        QTreeWidget* tree = dialog.tree();
        QTreeWidgetItem* root = tree->topLevelItem(0);
        QPushButton* ok = dialog.buttons()->button(QDialogButtonBox::Ok);
        // only folders are offered, files are not
        QTRY_COMPARE(root->childCount(), 2);
        QCOMPARE(root->child(0)->text(0), QStringLiteral("Docs"));
        QCOMPARE(root->child(1)->text(0), QStringLiteral("Photos"));

        const QString empty = f.tmp.path() + QStringLiteral("/dest");
        QVERIFY(QDir().mkpath(empty));
        dialog.setLocalPath(empty);

        // the whole drive cannot be chosen
        tree->setCurrentItem(root);
        QVERIFY(!ok->isEnabled());
        QVERIFY(dialog.driveFolderId().isEmpty());

        tree->setCurrentItem(root->child(0));
        QVERIFY(ok->isEnabled());
        QCOMPARE(dialog.driveFolderId(), docs);
        QCOMPARE(dialog.driveFolderName(), QStringLiteral("Docs"));

        // sub-folders load when expanded and can be chosen too
        root->child(0)->setExpanded(true);
        QTRY_VERIFY(
            root->child(0)->childCount() == 1 && root->child(0)->child(0)->text(0) == QLatin1String("2024"));
        tree->setCurrentItem(root->child(0)->child(0));
        QCOMPARE(dialog.driveFolderId(), year);
        QVERIFY(ok->isEnabled());

        // a destination that is not empty is refused, with an explanation
        QVERIFY(writeFile(f.tmp.path() + QStringLiteral("/full/file.txt"), "x"));
        dialog.setLocalPath(f.tmp.path() + QStringLiteral("/full"));
        QVERIFY(!ok->isEnabled());
        QVERIFY(dialog.validationLabel()->text().contains(QStringLiteral("must be empty")));
        dialog.setLocalPath(QString());
        QVERIFY(!ok->isEnabled());
        dialog.setLocalPath(empty);
        QVERIFY(ok->isEnabled());
        QVERIFY(dialog.validationLabel()->text().isEmpty());
    }

    void addingAFolderThroughTheWindowMountsIt()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        MainWindow window(f.core.get());
        window.show();
        QTest::mouseClick(child<QPushButton>(&window, "addButton"), Qt::LeftButton);
        auto* dialog = child<AddFolderDialog>(&window, "addFolderDialog");
        QVERIFY(dialog);
        QTRY_COMPARE(dialog->tree()->topLevelItem(0)->childCount(), 1);
        QTRY_VERIFY(dialog->tree()->topLevelItem(0)->child(0)->text(0) == QLatin1String("Docs"));
        dialog->tree()->setCurrentItem(dialog->tree()->topLevelItem(0)->child(0));
        dialog->setLocalPath(f.localDir(QStringLiteral("Docs")));
        QTest::mouseClick(dialog->buttons()->button(QDialogButtonBox::Ok), Qt::LeftButton);

        QCOMPARE(f.core->folders()->folders().size(), 1);
        QCOMPARE(f.core->folders()->folders().first().driveFolderId, docs);
        auto* list = child<QTreeWidget>(&window, "folderList");
        QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Docs"));
        QCOMPARE(list->topLevelItem(0)->text(1), f.localDir(QStringLiteral("Docs")));
        QVERIFY(f.places.places.contains(f.localDir(QStringLiteral("Docs")))); // shows up in Dolphin
    }

    void unmountAsksAndDeletesTheLocalCopy()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "a");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        MainWindow window(f.core.get());
        window.show();
        window.selectFolder(id);
        QSignalSpy removed(f.core->folders(), &FolderManager::folderRemoved);
        answerNextMessageBox(QDialogButtonBox::Yes); // "Unmount and Delete Local Copy"
        QTest::mouseClick(child<QPushButton>(&window, "unmountButton"), Qt::LeftButton);
        QVERIFY(removed.count() > 0 || removed.wait(10000));
        QVERIFY(!QFileInfo::exists(f.localDir(QStringLiteral("Docs"))));
        QCOMPARE(child<QTreeWidget>(&window, "folderList")->topLevelItemCount(), 0);
        QVERIFY(f.server.existsPath(QStringLiteral("Docs/a.txt")));
    }

    // ------------------------------------------------------------- conflicts
    void conflictDialogCollectsAChoicePerFile()
    {
        ConflictDialog dialog(QStringLiteral("Docs"),
            {{QStringLiteral("a.txt"), ConflictKind::BothModified},
                {QStringLiteral("b.txt"), ConflictKind::LocalModifiedRemoteDeleted}});
        QCOMPARE(dialog.table()->rowCount(), 2);
        QCOMPARE(dialog.table()->item(0, 0)->text(), QStringLiteral("a.txt"));
        QCOMPARE(dialog.choices().value(QStringLiteral("a.txt")),
            ConflictChoice::KeepBoth); // nothing is lost by default
        auto* combo = qobject_cast<QComboBox*>(dialog.table()->cellWidget(1, 2));
        combo->setCurrentIndex(combo->findData(int(ConflictChoice::KeepLocal)));
        QCOMPARE(dialog.choices().value(QStringLiteral("b.txt")), ConflictChoice::KeepLocal);
        dialog.setAll(ConflictChoice::KeepRemote);
        QCOMPARE(dialog.choices().value(QStringLiteral("a.txt")), ConflictChoice::KeepRemote);
    }

    void conflictIsAskedDuringSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        QObject::disconnect(f.core.get(), &AppCore::conflictsFound, nullptr, nullptr);
        MainWindow window(f.core.get());
        window.show();
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString a = f.server.addFile(QStringLiteral("a.txt"), docs, "base");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        window.selectFolder(id);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        const QString path = f.localDir(QStringLiteral("Docs")) + QStringLiteral("/a.txt");
        QVERIFY(writeFile(path, "local"));
        touchLater(path);
        f.server.setContent(a, "remote");

        finished.clear();
        QTest::mouseClick(child<QPushButton>(&window, "syncButton"), Qt::LeftButton);
        QTRY_VERIFY(child<ConflictDialog>(&window, "conflictDialog")
            && child<ConflictDialog>(&window, "conflictDialog")->isVisible());
        auto* dialog = child<ConflictDialog>(&window, "conflictDialog");
        QCOMPARE(dialog->table()->item(0, 0)->text(), QStringLiteral("a.txt"));
        dialog->setAll(ConflictChoice::KeepLocal);
        QTest::mouseClick(child<QPushButton>(dialog, "applyConflicts"), Qt::LeftButton);
        QVERIFY(finished.wait(10000));
        QCOMPARE(f.server.file(a).content, QByteArray("local"));
    }

    // --------------------------------------------------------------- account
    void signInAsksForDirectionAndSignOutClears()
    {
        Fixture f;
        f.createCore();
        MainWindow window(f.core.get());
        window.show();
        auto* login = child<QPushButton>(&window, "loginButton");
        auto* avatar = child<QToolButton>(&window, "accountButton");
        auto* account = child<QLabel>(&window, "accountLabel");
        QVERIFY(!login->isEnabled()); // wallet not opened yet
        QVERIFY(f.initialize());
        QVERIFY(login->isEnabled());
        QVERIFY(login->isVisible());
        QVERIFY(!avatar->isVisible());
        QCOMPARE(account->text(), QStringLiteral("Not signed in"));
        QVERIFY(!child<QPushButton>(&window, "addButton")->isEnabled());
        QVERIFY(!child<QPushButton>(&window, "syncAllButton")->isEnabled());

        // without an OAuth client the button opens the client settings first
        QTest::mouseClick(login, Qt::LeftButton);
        auto* credentials = window.findChild<CredentialsDialog*>();
        QVERIFY(credentials);
        child<QLineEdit>(credentials, "clientId")->setText(QStringLiteral("gui-client"));
        child<QLineEdit>(credentials, "clientSecret")->setText(QStringLiteral("gui-secret"));
        credentials->accept();
        QCOMPARE(f.secrets.read(SecretKeys::clientId()), QStringLiteral("gui-client"));

        f.server.setAccountEmail(QStringLiteral("carol@example.com"));
        f.server.setAccountName(QStringLiteral("Carol Example"));
        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        QTest::mouseClick(login, Qt::LeftButton);
        QVERIFY(ok.wait(10000));
        QCOMPARE(account->text(), QStringLiteral("Signed in as carol@example.com"));

        // the direction is asked right after the sign-in
        auto* direction = child<DirectionDialog>(&window, "directionDialog");
        QVERIFY(direction);
        QTRY_VERIFY(direction->isVisible());
        child<QRadioButton>(direction, "driveToLocal")->setChecked(true);
        direction->accept();
        QCOMPARE(f.core->direction(), SyncDirection::DriveToLocal);
        QVERIFY(child<QLabel>(&window, "directionLabel")->text().contains(QStringLiteral("Drive to local")));
        QVERIFY(child<QPushButton>(&window, "addButton")->isEnabled());

        // sign out from the account menu (after confirmation)
        answerNextMessageBox(QDialogButtonBox::Yes);
        window.findChild<QAction*>(QStringLiteral("actionSignOut"))->trigger();
        QVERIFY(!f.core->isLoggedIn());
        QCOMPARE(account->text(), QStringLiteral("Not signed in"));
        QVERIFY(login->isVisible());
        QVERIFY(!avatar->isVisible());
        QCOMPARE(login->text(), QStringLiteral("Sign In"));
        QVERIFY(!child<QPushButton>(&window, "addButton")->isEnabled());

        // sign in with another account
        f.server.setAccountEmail(QStringLiteral("dave@example.com"));
        ok.clear();
        QTest::mouseClick(login, Qt::LeftButton);
        QVERIFY(ok.wait(10000));
        QCOMPARE(account->text(), QStringLiteral("Signed in as dave@example.com"));
        QTRY_VERIFY(window.findChildren<DirectionDialog*>().size() >= 1); // asked again for the new session
    }

    void profilePictureReplacesTheSignInButton()
    {
        Fixture f;
        f.server.setAccountEmail(QStringLiteral("erin@example.com"));
        f.server.setAccountName(QStringLiteral("Erin <Example>"));
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        auto* login = child<QPushButton>(&window, "loginButton");
        auto* avatar = child<QToolButton>(&window, "accountButton");

        // signed in: no sign-in button, the picture is shown instead
        QVERIFY(!login->isVisible());
        QVERIFY(avatar->isVisible());
        QTRY_VERIFY(avatar->property("hasProfilePicture").toBool());
        QCOMPARE(f.server.requestCount(QStringLiteral("GET /photo")), 1);
        // it really is the downloaded picture (the mock serves a red image)
        const QImage shown = avatar->icon().pixmap(avatar->iconSize()).toImage();
        const QColor centre = shown.pixelColor(shown.width() / 2, shown.height() / 2);
        QCOMPARE(centre.red(), 255);
        QCOMPARE(centre.green(), 0);
        // and it is round: the corners are transparent
        QCOMPARE(shown.pixelColor(0, 0).alpha(), 0);

        // clicking it opens a menu with the user's name, e-mail address and a sign-out entry
        QCOMPARE(avatar->popupMode(), QToolButton::InstantPopup);
        QMenu* menu = avatar->menu();
        QVERIFY(menu);
        const QString info = child<QLabel>(menu, "accountMenuInfo")->text();
        QVERIFY(info.contains(QStringLiteral("Erin &lt;Example&gt;"))); // shown as text, not as markup
        QVERIFY(info.contains(QStringLiteral("erin@example.com")));
        const QAction* signOut = window.findChild<QAction*>(QStringLiteral("actionSignOut"));
        QVERIFY(menu->actions().contains(const_cast<QAction*>(signOut)));
        QCOMPARE(signOut->text(), QStringLiteral("Sign Out"));
    }

    void genericIconIsShownWhenTheAccountHasNoPicture()
    {
        Fixture f;
        f.server.setAccountHasPhoto(false);
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        auto* avatar = child<QToolButton>(&window, "accountButton");
        QVERIFY(avatar->isVisible());
        QTRY_VERIFY(!f.core->accountEmail().isEmpty()); // account details arrived
        QTest::qWait(200);
        QVERIFY(!avatar->property("hasProfilePicture").toBool());
        QCOMPARE(f.server.requestCount(QStringLiteral("GET /photo")), 0);
        QVERIFY(child<QLabel>(avatar->menu(), "accountMenuInfo")->text().contains(f.core->accountEmail()));
    }

    // -------------------------------------------------------------- settings
    void settingsMenuHoldsApiClientAndAutomaticSync()
    {
        Fixture f;
        f.createCore();
        MainWindow window(f.core.get());
        window.show();
        QVERIFY(f.initialize());
        auto* settings = child<QToolButton>(&window, "settingsButton");
        QVERIFY(settings->isVisible());
        QCOMPARE(settings->text(), QStringLiteral("Settings"));
        // the settings button comes right after the sign-in button
        auto* login = child<QPushButton>(&window, "loginButton");
        QVERIFY(settings->x() > login->x());
        QCOMPARE(settings->parentWidget(), login->parentWidget());

        QStringList entries;
        for (const QAction* a : settings->menu()->actions())
            entries.append(a->text());
        QCOMPARE(
            entries, (QStringList {QStringLiteral("Google API Client"), QStringLiteral("Automatic Sync")}));
        // these are no longer part of the main window itself
        QVERIFY(!child<QWidget>(&window, "credentialsButton"));
        QVERIFY(!child<QWidget>(&window, "autoSyncCheck"));

        QAction* apiClient = window.findChild<QAction*>(QStringLiteral("actionApiClient"));
        QAction* autoSync = window.findChild<QAction*>(QStringLiteral("actionAutoSync"));
        QVERIFY(apiClient->isEnabled());
        QVERIFY(!autoSync->isEnabled()); // needs a signed-in account

        // the dialog explains where the values are found in the Google Cloud console
        apiClient->trigger();
        auto* dialog = window.findChild<CredentialsDialog*>();
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Google API Client"));
        const QString help = child<QLabel>(dialog, "credentialsHelp")->text();
        QVERIFY(help.contains(QStringLiteral("console.cloud.google.com")));
        QVERIFY(help.contains(QStringLiteral("Google Auth platform")));
        QVERIFY(help.contains(QStringLiteral("Clients")));
        QVERIFY(help.contains(QStringLiteral("Desktop app")));
        QVERIFY(help.contains(QStringLiteral("Client ID")));
        QVERIFY(help.contains(QStringLiteral("Client secret")));
        dialog->reject();
    }

    void noButtonLabelContainsDots()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        AddFolderDialog addFolder(f.core.get());
        CredentialsDialog credentials {QString(), QString()};
        AutoSyncDialog autoSync(false, 0);
        DirectionDialog direction;
        DeletionDialog deletions(QStringLiteral("Docs"), {QStringLiteral("a")}, {QStringLiteral("b")});
        ConflictDialog conflicts(QStringLiteral("Docs"), {{QStringLiteral("a"), ConflictKind::BothModified}});

        int checked = 0;
        const auto check = [&checked](const QString& text) {
            ++checked;
            QVERIFY2(!text.contains(QLatin1Char('.')) && !text.contains(QChar(0x2026)), qPrintable(text));
        };
        const QList<QWidget*> roots {
            &window, &addFolder, &credentials, &autoSync, &direction, &deletions, &conflicts};
        for (QWidget* root : roots) {
            const auto buttons = root->findChildren<QAbstractButton*>();
            for (const QAbstractButton* b : buttons)
                check(b->text());
            const auto actions = root->findChildren<QAction*>();
            for (const QAction* a : actions) // menu entries behave like buttons
                check(a->text());
        }
        QVERIFY(checked > 20);
        QCOMPARE(window.findChild<QPushButton*>(QStringLiteral("addButton"))->text(),
            QStringLiteral("Add Folder"));
    }

    // ---------------------------------------------------------------- layout
    void syncedFoldersAreTheLargestAreaAndProgressIsSlim()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int folders = child<QWidget>(&window, "foldersBox")->height();
        const int progress = child<QWidget>(&window, "progressArea")->height();
        const int log = child<QWidget>(&window, "log")->height();
        QVERIFY2(folders > window.height() / 2, qPrintable(QString::number(folders)));
        QVERIFY(folders > 3 * progress);
        QVERIFY(folders > 2 * log);
        QVERIFY(child<QProgressBar>(&window, "progressBar")->height() <= 12);
        QVERIFY(progress < 70);

        // extra height goes to the folder list, not to the progress area
        window.resize(window.width(), window.height() + 300);
        QTRY_VERIFY(child<QWidget>(&window, "foldersBox")->height() >= folders + 250);
        QCOMPARE(child<QWidget>(&window, "progressArea")->height(), progress);
    }

    // -------------------------------------------------------- automatic sync
    void automaticSyncOptionInstallsTheTimer()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        QAction* autoSync = window.findChild<QAction*>(QStringLiteral("actionAutoSync"));
        QVERIFY(autoSync->isEnabled());
        QVERIFY(f.systemctlCalls.isEmpty());

        autoSync->trigger();
        auto* dialog = child<AutoSyncDialog>(&window, "autoSyncDialog");
        QVERIFY(dialog);
        auto* check = child<QCheckBox>(dialog, "autoSyncCheck");
        auto* interval = child<QSpinBox>(dialog, "autoSyncInterval");
        QVERIFY(!check->isChecked()); // off by default
        QSignalSpy changed(f.core.get(), &AppCore::autoSyncChanged);
        check->setChecked(true);
        interval->setValue(45);
        dialog->accept();
        QVERIFY(changed.wait(10000));
        QVERIFY(f.core->autoSyncEnabled());
        QCOMPARE(f.core->autoSyncIntervalMinutes(), 45);
        QVERIFY(f.systemctlCalls.contains(QStringLiteral("enable --now gdrive-sync.timer")));

        // the dialog shows the current state when opened again, and can switch it off
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        autoSync->trigger();
        dialog = window.findChildren<AutoSyncDialog*>().last();
        check = child<QCheckBox>(dialog, "autoSyncCheck");
        QVERIFY(check->isChecked());
        QCOMPARE(child<QSpinBox>(dialog, "autoSyncInterval")->value(), 45);
        changed.clear();
        check->setChecked(false);
        dialog->accept();
        QVERIFY(changed.wait(10000));
        QVERIFY(!f.core->autoSyncEnabled());
    }
};

QTEST_MAIN(TestGui)
#include "tst_gui.moc"
