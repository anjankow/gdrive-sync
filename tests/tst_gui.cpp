// GUI-level tests of the functional requirements: the widgets the user
// actually sees (folder selection, destination check, deletion warning,
// progress bar, sign-in/sign-out, automatic sync option).
#include "dialogs.h"
#include "mainwindow.h"
#include "testutil.h"

#include <QAction>
#include <QApplication>
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
#include <QWheelEvent>

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
    void windowTakesTheFocusAfterSignIn()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("gui-client"), QStringLiteral("gui-secret")));
        MainWindow window(f.core.get());
        window.show();
        // the browser is in front while the user signs in
        QWidget browser;
        browser.show();
        browser.activateWindow();
        window.showMinimized();
        QTRY_VERIFY(browser.isActiveWindow());

        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        f.core->login();
        QVERIFY(ok.wait(10000));
        QTRY_VERIFY(!window.isMinimized());
        QTRY_VERIFY(!browser.isActiveWindow());
        QWidget* active = QApplication::activeWindow();
        QVERIFY(active == &window || active->parentWidget() == &window); // the window or its dialog
    }

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

        // the whole drive can be chosen too
        tree->setCurrentItem(root);
        QVERIFY(ok->isEnabled());
        QCOMPARE(dialog.driveFolderId(), QStringLiteral("root"));
        QCOMPARE(dialog.driveFolderName(), QStringLiteral("My Drive"));

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

        // a destination that already holds files is accepted too
        QVERIFY(writeFile(f.tmp.path() + QStringLiteral("/full/file.txt"), "x"));
        dialog.setLocalPath(f.tmp.path() + QStringLiteral("/full"));
        QVERIFY(ok->isEnabled());
        QVERIFY(dialog.validationLabel()->text().isEmpty());
        // a file is not a folder
        dialog.setLocalPath(f.tmp.path() + QStringLiteral("/full/file.txt"));
        QVERIFY(!ok->isEnabled());
        QVERIFY(!dialog.validationLabel()->text().isEmpty());
        dialog.setLocalPath(QString());
        QVERIFY(!ok->isEnabled());
        dialog.setLocalPath(empty);
        QVERIFY(ok->isEnabled());
        QVERIFY(dialog.validationLabel()->text().isEmpty());
    }

    void statusColumnShowsJustSyncedAfterASuccessfulSync()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.txt"), docs, "a");
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        MainWindow window(f.core.get());
        window.show();
        auto* list = child<QTreeWidget>(&window, "folderList");
        QCOMPARE(list->headerItem()->text(3), QStringLiteral("Status"));
        QCOMPARE(f.sync(id).status, SyncResult::Success);
        QTRY_COMPARE(list->topLevelItem(0)->text(3), QStringLiteral("Synced"));
        // the details are in the log pane
        QVERIFY(
            child<QPlainTextEdit>(&window, "log")->toPlainText().contains(QStringLiteral("1 downloaded")));

        // a failure keeps its reason
        f.server.failNext(QStringLiteral("/drive/v3/files"), 500, 50);
        QCOMPARE(f.sync(id).status, SyncResult::Failed);
        QTRY_VERIFY(list->topLevelItem(0)->text(3).startsWith(QStringLiteral("Failed: ")));

        // a status stored by an earlier version is shortened too
        f.core->folders()->recordSync(id, QStringLiteral("Synced: 1 downloaded, 0 uploaded (5 B)"));
        f.core->flushSettings();
        f.createCore();
        QVERIFY(f.initialize());
        QCOMPARE(f.core->folders()->find(id)->lastStatus, QStringLiteral("Synced"));
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

    // ------------------------------------------------------------ directions
    void folderListHasADirectionDropdownPerFolder()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString a = f.mount(f.server.addFolder(QStringLiteral("A")), QStringLiteral("A"));
        f.core->setDefaultDirection(SyncDirection::DriveToLocal);
        const QString b = f.mount(f.server.addFolder(QStringLiteral("B")), QStringLiteral("B"));
        MainWindow window(f.core.get());
        window.show();
        auto* list = child<QTreeWidget>(&window, "folderList");
        // the direction is the last column
        QCOMPARE(list->columnCount(), 5);
        QCOMPARE(list->headerItem()->text(4), QStringLiteral("Direction"));
        const auto comboOf = [&](const QString& id) -> QComboBox* {
            for (int i = 0; i < list->topLevelItemCount(); ++i) {
                auto* combo = qobject_cast<QComboBox*>(list->itemWidget(list->topLevelItem(i), 4));
                if (combo && combo->property("folderId").toString() == id)
                    return combo;
            }
            return nullptr;
        };
        QVERIFY(comboOf(a) && comboOf(b));
        // all three directions are offered; each folder shows its own
        QCOMPARE(comboOf(a)->count(), 3);
        QCOMPARE(comboOf(a)->currentText(), QStringLiteral("Two-way"));
        QCOMPARE(comboOf(b)->currentText(), QStringLiteral("Drive to local"));

        // choosing another entry changes that folder only
        QComboBox* combo = comboOf(a);
        const int index = combo->findText(QStringLiteral("Local to Drive"));
        combo->setCurrentIndex(index);
        Q_EMIT combo->activated(index); // what a click on the entry does
        QTRY_COMPARE(f.core->folderDirection(a), SyncDirection::LocalToDrive);
        QCOMPARE(f.core->folderDirection(b), SyncDirection::DriveToLocal);
        QTRY_COMPARE(comboOf(a)->currentText(), QStringLiteral("Local to Drive")); // the list was rebuilt
        QVERIFY(child<QPlainTextEdit>(&window, "log")
                    ->toPlainText()
                    .contains(QStringLiteral("Sync direction of \"A\": Local to Drive")));

        // the mouse wheel over the list does not change a direction
        QWheelEvent wheel(QPointF(5, 5), comboOf(a)->mapToGlobal(QPoint(5, 5)), QPoint(), QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(comboOf(a), &wheel);
        QCOMPARE(comboOf(a)->currentText(), QStringLiteral("Local to Drive"));

        // not changeable while a sync is running
        f.server.addFile(
            QStringLiteral("big.bin"), f.server.idByPath(QStringLiteral("A")), QByteArray(2000000, 'x'));
        QVERIFY(f.core->setFolderDirection(a, SyncDirection::TwoWay));
        f.server.setDownloadChunkDelayMs(10);
        QSignalSpy finished(f.core.get(), &AppCore::syncFinished);
        QVERIFY(f.core->syncFolder(a));
        QTRY_VERIFY(!comboOf(a)->isEnabled());
        f.core->cancelSync();
        QVERIFY(finished.count() > 0 || finished.wait(10000));
        QTRY_VERIFY(comboOf(a)->isEnabled());
    }

    void defaultSyncDirectionIsChangedInTheSettings()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        QAction* action = window.findChild<QAction*>(QStringLiteral("actionDefaultDirection"));
        QVERIFY(action);
        QCOMPARE(action->text(), QStringLiteral("Default Sync Direction"));

        action->trigger();
        auto* dialog = child<DirectionDialog>(&window, "directionDialog");
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Default Sync Direction"));
        QCOMPARE(dialog->direction(), SyncDirection::TwoWay); // two-way unless changed
        child<QRadioButton>(dialog, "localToDrive")->setChecked(true);
        dialog->accept();
        QCOMPARE(f.core->defaultDirection(), SyncDirection::LocalToDrive);

        // a folder added now gets it; the dialog shows the current default next time
        const QString id = f.mount(f.server.addFolder(QStringLiteral("Docs")), QStringLiteral("Docs"));
        QCOMPARE(f.core->folderDirection(id), SyncDirection::LocalToDrive);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        action->trigger();
        dialog = window.findChildren<DirectionDialog*>().last();
        QCOMPARE(dialog->direction(), SyncDirection::LocalToDrive);
        // cancelling changes nothing
        child<QRadioButton>(dialog, "twoWay")->setChecked(true);
        dialog->reject();
        QCOMPARE(f.core->defaultDirection(), SyncDirection::LocalToDrive);
    }

    void unmountOfANonEmptyFolderOffersToKeepTheLocalFiles()
    {
        // button of the question: Yes = keep all local files, No = delete all of them
        for (const auto answer : {QDialogButtonBox::Yes, QDialogButtonBox::No, QDialogButtonBox::Cancel}) {
            Fixture f;
            QVERIFY(f.startLoggedIn());
            const QString docs = f.server.addFolder(QStringLiteral("Docs"));
            f.server.addFile(QStringLiteral("a.txt"), docs, "a");
            const QString dir = f.localDir(QStringLiteral("Docs"));
            QVERIFY(writeFile(dir + QStringLiteral("/mine.txt"), "m"));
            const QString id = f.mount(docs, QStringLiteral("Docs"));
            QCOMPARE(f.sync(id).status, SyncResult::Success);
            MainWindow window(f.core.get());
            window.show();
            window.selectFolder(id);
            QSignalSpy removed(f.core->folders(), &FolderManager::folderRemoved);

            QStringList buttons;
            auto* watcher = new QTimer(&window);
            QObject::connect(watcher, &QTimer::timeout, watcher, [&buttons, watcher, answer]() {
                QWidget* modal = QApplication::activeModalWidget();
                auto* box = modal ? modal->findChild<QDialogButtonBox*>() : nullptr;
                if (!box || !box->button(answer))
                    return;
                for (QAbstractButton* b : box->buttons())
                    buttons.append(b->text().remove(QLatin1Char('&')));
                watcher->stop();
                box->button(answer)->click();
            });
            watcher->start(20);
            QTest::mouseClick(child<QPushButton>(&window, "unmountButton"), Qt::LeftButton);

            QVERIFY(buttons.contains(QStringLiteral("Unmount and Keep Local Files")));
            QVERIFY(buttons.contains(QStringLiteral("Unmount and Delete All Local Files")));
            if (answer == QDialogButtonBox::Cancel) {
                QTest::qWait(200);
                QCOMPARE(f.core->folders()->folders().size(), 1);
                QVERIFY(QFile::exists(dir + QStringLiteral("/mine.txt")));
                continue;
            }
            QVERIFY(removed.count() > 0 || removed.wait(10000));
            QCOMPARE(child<QTreeWidget>(&window, "folderList")->topLevelItemCount(), 0);
            const bool kept = answer == QDialogButtonBox::Yes;
            QCOMPARE(QFile::exists(dir + QStringLiteral("/mine.txt")), kept);
            QCOMPARE(QFile::exists(dir + QStringLiteral("/a.txt")), kept); // all files, not only the own ones
            QTRY_VERIFY(child<QPlainTextEdit>(&window, "log")
                            ->toPlainText()
                            .contains(kept ? QStringLiteral("the local files were kept")
                                           : QStringLiteral("the local copy was deleted")));
        }
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
    void cancellingTheSignInShowsNoMessage()
    {
        Fixture f;
        f.secrets.preset(SecretKeys::clientId(), QStringLiteral("saved-client"));
        f.browser.neverReturns = true; // the user leaves the Google page open
        f.createCore();
        MainWindow window(f.core.get());
        window.show();
        QVERIFY(f.initialize());
        auto* login = child<QPushButton>(&window, "loginButton");

        QTest::mouseClick(login, Qt::LeftButton);
        QTRY_COMPARE(f.browser.opened.size(), 1);
        QCOMPARE(login->text(), QStringLiteral("Cancel Sign-in"));
        QSignalSpy cancelled(f.core.get(), &AppCore::loginCancelled);
        QTest::mouseClick(login, Qt::LeftButton);
        QCOMPARE(cancelled.count(), 1);
        QTest::qWait(300); // a message box would be opened from the event loop
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(login->text(), QStringLiteral("Sign In"));
        QVERIFY(!f.core->isLoginInProgress());
        const QString log = child<QPlainTextEdit>(&window, "log")->toPlainText();
        QVERIFY(log.contains(QStringLiteral("Sign-in cancelled.")));
        QVERIFY(!log.contains(QStringLiteral("Sign-in failed")));
    }

    void signInGoesStraightToTheBrowserWhenTheClientIsSaved()
    {
        Fixture f;
        // the client was saved earlier (also by versions that kept no note of it in the settings)
        f.secrets.preset(SecretKeys::clientId(), QStringLiteral("saved-client"));
        f.secrets.preset(SecretKeys::clientSecret(), QStringLiteral("saved-secret"));
        f.createCore();
        MainWindow window(f.core.get());
        window.show();
        QVERIFY(f.initialize());
        QVERIFY(!f.core->hasClientCredentials());

        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        QTest::mouseClick(child<QPushButton>(&window, "loginButton"), Qt::LeftButton);
        QVERIFY(ok.wait(10000));
        QCOMPARE(f.browser.opened.size(), 1);
        QVERIFY(window.findChildren<CredentialsDialog*>().isEmpty()); // no client settings asked
        QCOMPARE(f.server.lastAuthorizationQuery().value(QStringLiteral("client_id")),
            QStringLiteral("saved-client"));

        // signing out and in again does not ask for the client either
        answerNextMessageBox(QDialogButtonBox::Yes);
        window.findChild<QAction*>(QStringLiteral("actionSignOut"))->trigger();
        QVERIFY(!f.core->isLoggedIn());
        QTRY_VERIFY(!f.secrets.isOpen());
        // the closed wallet is opened again by the sign-in
        ok.clear();
        QTest::mouseClick(child<QPushButton>(&window, "loginButton"), Qt::LeftButton);
        QVERIFY(ok.wait(10000));
        QCOMPARE(f.browser.opened.size(), 2);
        QVERIFY(window.findChildren<CredentialsDialog*>().isEmpty());
    }

    void signInAndSignOutThroughTheWindow()
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
        QTRY_VERIFY(window.findChild<CredentialsDialog*>()); // after the wallet is open
        auto* credentials = window.findChild<CredentialsDialog*>();
        child<QLineEdit>(credentials, "clientId")->setText(QStringLiteral("gui-client"));
        child<QLineEdit>(credentials, "clientSecret")->setText(QStringLiteral("gui-secret"));
        credentials->accept();
        QCOMPARE(f.secrets.stored(SecretKeys::clientId()), QStringLiteral("gui-client"));

        f.server.setAccountEmail(QStringLiteral("carol@example.com"));
        f.server.setAccountName(QStringLiteral("Carol Example"));
        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        QTest::mouseClick(login, Qt::LeftButton);
        QVERIFY(ok.wait(10000));
        // who is signed in is shown by the profile picture's menu, not by a label
        QVERIFY(!account->isVisible());
        QVERIFY(child<QLabel>(avatar->menu(), "accountMenuInfo")
                    ->text()
                    .contains(QStringLiteral("carol@example.com")));
        const auto labels = window.findChildren<QLabel*>();
        for (const QLabel* label : labels)
            QVERIFY2(!label->text().contains(QStringLiteral("Signed in as")), qPrintable(label->text()));

        // no direction is asked after the sign-in: folders can be added right away
        QTest::qWait(100);
        QVERIFY(window.findChildren<DirectionDialog*>().isEmpty());
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
        QTRY_VERIFY(child<QLabel>(avatar->menu(), "accountMenuInfo")
                        ->text()
                        .contains(QStringLiteral("dave@example.com")));
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
        QCOMPARE(entries,
            (QStringList {QStringLiteral("Google API Client"), QStringLiteral("Automatic Sync"),
                QStringLiteral("Default Sync Direction"), QStringLiteral("Log Location")}));
        // these are no longer part of the main window itself
        QVERIFY(!child<QWidget>(&window, "credentialsButton"));
        QVERIFY(!child<QWidget>(&window, "autoSyncCheck"));

        QAction* apiClient = window.findChild<QAction*>(QStringLiteral("actionApiClient"));
        QAction* autoSync = window.findChild<QAction*>(QStringLiteral("actionAutoSync"));
        QVERIFY(apiClient->isEnabled());
        QVERIFY(!autoSync->isEnabled()); // needs a signed-in account

        // the dialog explains where the values are found in the Google Cloud console
        apiClient->trigger();
        QTRY_VERIFY(window.findChild<CredentialsDialog*>()); // after the wallet is open
        auto* dialog = window.findChild<CredentialsDialog*>();
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Google API Client"));
        const QString help = child<QLabel>(dialog, "credentialsHelp")->text();
        // create a project -> enable the Drive API -> Create credentials -> for the Drive API -> scopes
        const QStringList steps {QStringLiteral("Create a Google Cloud project"),
            QStringLiteral("Enable the <b>Google Drive API</b>"), QStringLiteral("Create credentials"),
            QStringLiteral("choose <b>Google Drive API</b>"),
            QStringLiteral("https://www.googleapis.com/auth/drive"), QStringLiteral("Desktop app")};
        int position = -1;
        for (const QString& step : steps) {
            const int found = help.indexOf(step, position + 1);
            QVERIFY2(found > position, qPrintable(step)); // present and in this order
            position = found;
        }
        QVERIFY(help.contains(QStringLiteral("console.cloud.google.com")));
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
        LogLocationDialog logLocation(QStringLiteral("/tmp/x"), QStringLiteral("/tmp/y"));
        DeletionDialog deletions(QStringLiteral("Docs"), {QStringLiteral("a")}, {QStringLiteral("b")});
        ConflictDialog conflicts(QStringLiteral("Docs"), {{QStringLiteral("a"), ConflictKind::BothModified}});

        int checked = 0;
        const auto check = [&checked](const QString& text) {
            ++checked;
            QVERIFY2(!text.contains(QLatin1Char('.')) && !text.contains(QChar(0x2026)), qPrintable(text));
        };
        const QList<QWidget*> roots {
            &window, &addFolder, &credentials, &autoSync, &direction, &logLocation, &deletions, &conflicts};
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

    void walletFailureWhenSavingTheClientIsShownAsAnError()
    {
        Fixture f;
        f.createCore();
        MainWindow window(f.core.get());
        window.show();
        QVERIFY(f.initialize());
        f.secrets.failWrites = true;
        f.secrets.failReason
            = QStringLiteral("the KDE wallet was closed or locked after the application opened it");

        window.findChild<QAction*>(QStringLiteral("actionApiClient"))->trigger();
        QTRY_VERIFY(window.findChild<CredentialsDialog*>()); // after the wallet is open
        auto* dialog = window.findChild<CredentialsDialog*>();
        child<QLineEdit>(dialog, "clientId")->setText(QStringLiteral("some-id"));
        // the error is also shown in a message box, once the dialog is closed
        answerNextMessageBox(QDialogButtonBox::Ok);
        dialog->accept();
        QTest::qWait(400);
        QVERIFY(!QApplication::activeModalWidget()); // the box was shown and answered

        const QString log = child<QPlainTextEdit>(&window, "log")->toPlainText();
        const QStringList lines = log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString line = lines.last();
        // marked as an error, with the reason and with what happened to the change
        QVERIFY2(
            QRegularExpression(QStringLiteral("^\\[\\d\\d:\\d\\d:\\d\\d\\] ERROR ")).match(line).hasMatch(),
            qPrintable(line));
        QVERIFY(line.contains(QStringLiteral("could not be stored in the KDE wallet")));
        QVERIFY(line.contains(QStringLiteral("the KDE wallet was closed or locked")));
        QVERIFY(line.contains(QStringLiteral("The change was aborted")));
        QVERIFY(!f.core->hasClientCredentials());

        // a successful save is an INFO line
        f.secrets.failWrites = false;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.findChild<QAction*>(QStringLiteral("actionApiClient"))->trigger();
        QTRY_VERIFY(window.findChild<CredentialsDialog*>()); // after the wallet is open
        dialog = window.findChildren<CredentialsDialog*>().last();
        child<QLineEdit>(dialog, "clientId")->setText(QStringLiteral("some-id"));
        dialog->accept();
        QVERIFY(child<QPlainTextEdit>(&window, "log")
                    ->toPlainText()
                    .split(QLatin1Char('\n'))
                    .last()
                    .contains(QStringLiteral("] INFO  Google API client saved")));
        QVERIFY(f.core->hasClientCredentials());
    }

    void logLocationIsChosenInTheSettingsMenu()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        MainWindow window(f.core.get());
        window.show();
        // by default the log is written below the temp directory
        QCOMPARE(f.core->logDirectory(), f.paths().logDir);

        window.findChild<QAction*>(QStringLiteral("actionLogLocation"))->trigger();
        auto* dialog = child<LogLocationDialog>(&window, "logLocationDialog");
        QVERIFY(dialog);
        auto* edit = child<QLineEdit>(dialog, "logDirectory");
        QCOMPARE(edit->text(), f.paths().logDir);
        // the dialog says only what the folder is for
        QCOMPARE(child<QLabel>(dialog, "logLocationLabel")->text(),
            QStringLiteral("Folder for the log file gdrive-sync.log:"));
        for (const QLabel* label : dialog->findChildren<QLabel*>())
            QVERIFY(!label->text().contains(QStringLiteral("DEBUG"))
                && !label->text().contains(QStringLiteral("INFO")));
        const QString chosen = f.tmp.path() + QStringLiteral("/my-logs");
        edit->setText(chosen);
        dialog->accept();
        QCOMPARE(f.core->logDirectory(), chosen);
        QCOMPARE(f.core->logFilePath(), chosen + QStringLiteral("/gdrive-sync.log"));
        QVERIFY(QFile::exists(chosen + QStringLiteral("/gdrive-sync.log")));

        // "Default" restores the standard location
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.findChild<QAction*>(QStringLiteral("actionLogLocation"))->trigger();
        dialog = window.findChildren<LogLocationDialog*>().last();
        QTest::mouseClick(child<QPushButton>(dialog, "logDirectoryDefault"), Qt::LeftButton);
        QCOMPARE(child<QLineEdit>(dialog, "logDirectory")->text(), f.paths().logDir);
        dialog->accept();
        QCOMPARE(f.core->logDirectory(), f.paths().logDir);
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
