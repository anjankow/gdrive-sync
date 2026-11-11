#include "mainwindow.h"

#include "dialogs.h"
#include "format.h"

#include <KLocalizedString>
#include <KMessageBox>

#include <QCheckBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace gds {

namespace {
    enum { FolderIdRole = Qt::UserRole + 1 };

    QPushButton* button(const QString& icon, const QString& text, const char* name, QWidget* parent)
    {
        auto* b = new QPushButton(QIcon::fromTheme(icon), text, parent);
        b->setObjectName(QLatin1String(name));
        return b;
    }
}

MainWindow::MainWindow(AppCore* core, QWidget* parent)
    : QMainWindow(parent)
    , m_core(core)
{
    setWindowTitle(i18nc("@title:window", "Google Drive Sync"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("folder-cloud")));
    openFolder = [](const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); };

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    // ---- account
    auto* accountBox = new QGroupBox(i18n("Account"), central);
    auto* accountLayout = new QHBoxLayout(accountBox);
    auto* accountLabels = new QVBoxLayout;
    m_account = new QLabel(accountBox);
    m_account->setObjectName(QStringLiteral("accountLabel"));
    m_direction = new QLabel(accountBox);
    m_direction->setObjectName(QStringLiteral("directionLabel"));
    accountLabels->addWidget(m_account);
    accountLabels->addWidget(m_direction);
    accountLayout->addLayout(accountLabels, 1);
    m_credentials
        = button(QStringLiteral("configure"), i18n("Google API Client…"), "credentialsButton", accountBox);
    m_login = button(QStringLiteral("im-user"), i18n("Sign In"), "loginButton", accountBox);
    accountLayout->addWidget(m_credentials);
    accountLayout->addWidget(m_login);
    layout->addWidget(accountBox);

    // ---- folders
    auto* foldersBox = new QGroupBox(i18n("Synced Folders"), central);
    auto* foldersLayout = new QVBoxLayout(foldersBox);
    m_folders = new QTreeWidget(foldersBox);
    m_folders->setObjectName(QStringLiteral("folderList"));
    m_folders->setRootIsDecorated(false);
    m_folders->setHeaderLabels(
        {i18n("Google Drive folder"), i18n("Local folder"), i18n("Last sync"), i18n("Status")});
    m_folders->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_folders->header()->setStretchLastSection(true);
    foldersLayout->addWidget(m_folders);
    auto* folderButtons = new QHBoxLayout;
    m_add = button(QStringLiteral("folder-new"), i18n("Add Folder…"), "addButton", foldersBox);
    m_unmount = button(QStringLiteral("edit-delete"), i18n("Unmount"), "unmountButton", foldersBox);
    m_open = button(
        QStringLiteral("document-open-folder"), i18n("Open in File Manager"), "openButton", foldersBox);
    m_sync = button(QStringLiteral("view-refresh"), i18n("Sync Selected"), "syncButton", foldersBox);
    m_syncAll = button(QStringLiteral("view-refresh"), i18n("Sync All"), "syncAllButton", foldersBox);
    folderButtons->addWidget(m_add);
    folderButtons->addWidget(m_unmount);
    folderButtons->addWidget(m_open);
    folderButtons->addStretch(1);
    folderButtons->addWidget(m_sync);
    folderButtons->addWidget(m_syncAll);
    foldersLayout->addLayout(folderButtons);
    layout->addWidget(foldersBox, 2);

    // ---- progress
    auto* progressBox = new QGroupBox(i18n("Progress"), central);
    auto* progressLayout = new QVBoxLayout(progressBox);
    auto* barRow = new QHBoxLayout;
    m_progress = new QProgressBar(progressBox);
    m_progress->setObjectName(QStringLiteral("progressBar"));
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_cancel = button(QStringLiteral("process-stop"), i18n("Cancel"), "cancelButton", progressBox);
    barRow->addWidget(m_progress, 1);
    barRow->addWidget(m_cancel);
    progressLayout->addLayout(barRow);
    m_progressText = new QLabel(i18n("Idle"), progressBox);
    m_progressText->setObjectName(QStringLiteral("progressText"));
    m_currentFile = new QLabel(progressBox);
    m_currentFile->setObjectName(QStringLiteral("currentFile"));
    m_currentFile->setTextFormat(Qt::PlainText);
    progressLayout->addWidget(m_progressText);
    progressLayout->addWidget(m_currentFile);
    layout->addWidget(progressBox);

    // ---- automatic sync
    auto* autoBox = new QGroupBox(i18n("Automatic Sync"), central);
    auto* autoLayout = new QHBoxLayout(autoBox);
    m_autoSync = new QCheckBox(i18n("Sync all folders automatically every"), autoBox);
    m_autoSync->setObjectName(QStringLiteral("autoSyncCheck"));
    m_interval = new QSpinBox(autoBox);
    m_interval->setObjectName(QStringLiteral("autoSyncInterval"));
    m_interval->setRange(5, 24 * 60);
    m_interval->setValue(30);
    m_interval->setSuffix(i18n(" minutes"));
    m_applyAuto = button(QStringLiteral("dialog-ok-apply"), i18n("Apply"), "autoSyncApply", autoBox);
    autoLayout->addWidget(m_autoSync);
    autoLayout->addWidget(m_interval);
    autoLayout->addStretch(1);
    autoLayout->addWidget(m_applyAuto);
    autoBox->setToolTip(
        i18n("Installs a systemd user timer. Automatic runs never delete files and never resolve "
             "conflicts; those are left for the next manual sync."));
    layout->addWidget(autoBox);

    // ---- log
    m_log = new QPlainTextEdit(central);
    m_log->setObjectName(QStringLiteral("log"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    layout->addWidget(m_log, 1);

    setCentralWidget(central);
    resize(900, 680);

    connect(m_login, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(m_credentials, &QPushButton::clicked, this, &MainWindow::editCredentials);
    connect(m_add, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(m_unmount, &QPushButton::clicked, this, &MainWindow::unmountSelected);
    connect(m_open, &QPushButton::clicked, this, [this]() {
        if (const FolderConfig* f = m_core->folders()->find(selectedFolderId()))
            openFolder(f->localPath);
    });
    connect(m_sync, &QPushButton::clicked, this, &MainWindow::syncSelected);
    connect(m_syncAll, &QPushButton::clicked, this, &MainWindow::syncAll);
    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        m_core->cancelSync();
        m_progressText->setText(i18n("Cancelling…"));
    });
    connect(m_applyAuto, &QPushButton::clicked, this, &MainWindow::applyAutoSync);
    connect(m_folders, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshButtons);

    connect(m_core, &AppCore::initialized, this, &MainWindow::onInitialized);
    connect(m_core, &AppCore::accountChanged, this, &MainWindow::refreshAccount);
    connect(m_core, &AppCore::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_core, &AppCore::loginFailed, this, [this](const QString& error) {
        refreshAccount();
        log(i18n("Sign-in failed: %1", error));
        KMessageBox::error(this, error, i18nc("@title:window", "Sign-in Failed"), KMessageBox::Notify);
    });
    connect(m_core, &AppCore::loggedOut, this, [this]() { log(i18n("Signed out.")); });
    connect(m_core, &AppCore::authorizationLost, this, [this]() {
        log(i18n("Google rejected the stored sign-in. Please sign in again."));
        refreshAccount();
    });
    connect(m_core->folders(), &FolderManager::foldersChanged, this, &MainWindow::refreshFolders);
    connect(m_core->folders(), &FolderManager::folderRemoved, this,
        [this](const QString&, bool ok, const QString& error) {
            m_unmounting = false;
            log(ok ? i18n("Folder unmounted; the local copy was deleted.") : error);
            refreshButtons();
        });
    connect(m_core, &AppCore::syncStarted, this, [this]() {
        m_progress->setValue(0);
        refreshButtons();
    });
    connect(m_core, &AppCore::folderSyncStarted, this,
        [this](const QString& id) { log(i18n("Syncing \"%1\"…", folderName(id))); });
    connect(m_core, &AppCore::progress, this, &MainWindow::onProgress);
    connect(m_core, &AppCore::conflictsFound, this, &MainWindow::onConflicts);
    connect(m_core, &AppCore::deletionsPending, this, &MainWindow::onDeletions);
    connect(m_core, &AppCore::folderSyncFinished, this, &MainWindow::onFolderFinished);
    connect(m_core, &AppCore::syncFinished, this, &MainWindow::onSyncFinished);
    connect(m_core, &AppCore::autoSyncChanged, this, [this](bool ok, const QString& error) {
        m_autoSyncBusy = false;
        if (!ok) {
            log(i18n("Automatic sync could not be changed: %1", error));
            KMessageBox::error(this, error, i18nc("@title:window", "Automatic Sync"), KMessageBox::Notify);
        }
        refreshAutoSync();
    });

    refreshAccount();
    refreshFolders();
    refreshAutoSync();
}

QString MainWindow::selectedFolderId() const
{
    const auto selected = m_folders->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->data(0, FolderIdRole).toString();
}

void MainWindow::selectFolder(const QString& id)
{
    for (int i = 0; i < m_folders->topLevelItemCount(); ++i) {
        if (m_folders->topLevelItem(i)->data(0, FolderIdRole).toString() == id)
            m_folders->setCurrentItem(m_folders->topLevelItem(i));
    }
}

QString MainWindow::folderName(const QString& id) const
{
    const FolderConfig* f = m_core->folders()->find(id);
    return f ? f->driveFolderName : id;
}

void MainWindow::log(const QString& text)
{
    m_log->appendPlainText(
        QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void MainWindow::refreshAccount()
{
    if (!m_core->isInitialized()) {
        m_account->setText(i18n("Opening the KDE wallet…"));
        m_direction->clear();
    } else if (m_core->isLoginInProgress()) {
        m_account->setText(i18n("Waiting for the sign-in in your web browser…"));
    } else if (m_core->isLoggedIn()) {
        m_account->setText(i18n("Signed in as %1", m_core->accountEmail()));
        m_direction->setText(i18n("Sync direction: %1", directionLabel(m_core->direction())));
    } else {
        m_account->setText(i18n("Not signed in"));
        m_direction->clear();
    }
    m_login->setText(m_core->isLoginInProgress() ? i18n("Cancel Sign-in")
            : m_core->isLoggedIn()               ? i18n("Sign Out")
                                                 : i18n("Sign In"));
    refreshButtons();
}

void MainWindow::refreshFolders()
{
    const QString selected = selectedFolderId();
    m_folders->clear();
    const auto folders = m_core->folders()->folders();
    for (const FolderConfig& f : folders) {
        auto* item = new QTreeWidgetItem(m_folders);
        item->setData(0, FolderIdRole, f.id);
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-cloud")));
        item->setText(0, f.driveFolderName);
        item->setText(1, f.localPath);
        item->setText(
            2, f.lastSync.isValid() ? QLocale().toString(f.lastSync, QLocale::ShortFormat) : i18n("Never"));
        item->setText(3, f.lastStatus);
    }
    selectFolder(selected);
    if (m_folders->selectedItems().isEmpty() && m_folders->topLevelItemCount() > 0)
        m_folders->setCurrentItem(m_folders->topLevelItem(0));
    refreshButtons();
}

void MainWindow::refreshButtons()
{
    const bool ready = m_core->isInitialized();
    const bool loggedIn = m_core->isLoggedIn();
    const bool syncing = m_core->isSyncing();
    const bool hasSelection = !selectedFolderId().isEmpty();
    const bool hasFolders = !m_core->folders()->folders().isEmpty();
    const bool idle = loggedIn && !syncing && !m_unmounting;
    m_login->setEnabled(ready);
    m_credentials->setEnabled(ready && !loggedIn && !m_core->isLoginInProgress());
    m_add->setEnabled(idle);
    m_unmount->setEnabled(idle && hasSelection);
    m_open->setEnabled(hasSelection);
    m_sync->setEnabled(idle && hasSelection);
    m_syncAll->setEnabled(idle && hasFolders);
    m_cancel->setEnabled(syncing);
    m_autoSync->setEnabled(loggedIn && !m_autoSyncBusy);
    m_interval->setEnabled(loggedIn && !m_autoSyncBusy);
    m_applyAuto->setEnabled(loggedIn && !m_autoSyncBusy);
}

void MainWindow::refreshAutoSync()
{
    const bool enabled = m_core->autoSyncEnabled();
    m_autoSync->setChecked(enabled);
    if (enabled && m_core->autoSyncIntervalMinutes() > 0)
        m_interval->setValue(m_core->autoSyncIntervalMinutes());
    refreshButtons();
}

void MainWindow::onInitialized(bool ok, const QString& error)
{
    refreshAccount();
    refreshAutoSync();
    if (!ok) {
        m_account->setText(i18n("KDE wallet not available"));
        log(error);
        KMessageBox::error(this, error, i18nc("@title:window", "KDE Wallet"), KMessageBox::Notify);
        return;
    }
    if (m_core->isLoggedIn() && m_core->direction() == SyncDirection::Unset)
        askDirection();
}

void MainWindow::editCredentials()
{
    auto* dialog = new CredentialsDialog(m_core->clientId(), m_core->clientSecret(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        if (!m_core->setClientCredentials(dialog->clientId(), dialog->clientSecret()))
            log(i18n("The Google API client could not be stored in the KDE wallet."));
        refreshButtons();
    });
    dialog->open();
}

void MainWindow::onLoginClicked()
{
    if (m_core->isLoginInProgress()) {
        m_core->cancelLogin();
    } else if (m_core->isLoggedIn()) {
        const auto answer = KMessageBox::warningContinueCancel(this,
            i18n("Sign out of %1?\n\nThe stored sign-in is revoked and the list of synced folders and the "
                 "automatic sync are removed. Files already on this computer are kept.",
                m_core->accountEmail()),
            i18nc("@title:window", "Sign Out"), KGuiItem(i18n("Sign Out"), QStringLiteral("system-log-out")));
        if (answer == KMessageBox::Continue)
            m_core->logout();
    } else if (!m_core->hasClientCredentials()) {
        editCredentials();
    } else {
        m_core->login();
        log(i18n("Complete the sign-in in your web browser."));
    }
    refreshAccount();
}

void MainWindow::onLoginSucceeded()
{
    log(i18n("Signed in as %1.", m_core->accountEmail()));
    refreshAccount();
    if (m_core->direction() == SyncDirection::Unset)
        askDirection();
}

void MainWindow::askDirection()
{
    auto* dialog = new DirectionDialog(this);
    dialog->setObjectName(QStringLiteral("directionDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        m_core->setDirection(dialog->direction());
        log(i18n("Sync direction: %1", directionLabel(dialog->direction())));
    });
    dialog->open();
}

void MainWindow::addFolder()
{
    auto* dialog = new AddFolderDialog(m_core, this);
    dialog->setObjectName(QStringLiteral("addFolderDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        QString error;
        const QString id = m_core->folders()->addFolder(
            dialog->driveFolderId(), dialog->driveFolderName(), dialog->localPath(), &error);
        if (id.isEmpty()) {
            KMessageBox::error(this, error, i18nc("@title:window", "Add Folder"), KMessageBox::Notify);
            return;
        }
        log(i18n("Added \"%1\". Press \"Sync Selected\" to download it.", dialog->driveFolderName()));
        selectFolder(id);
    });
    dialog->open();
}

void MainWindow::unmountSelected()
{
    const FolderConfig* f = m_core->folders()->find(selectedFolderId());
    if (!f)
        return;
    const QString id = f->id;
    const auto answer = KMessageBox::warningContinueCancel(this,
        i18n("Unmount \"%1\"?\n\nThe local copy in %2 will be deleted. Nothing is deleted from Google Drive.",
            f->driveFolderName, f->localPath),
        i18nc("@title:window", "Unmount Folder"),
        KGuiItem(i18n("Unmount and Delete Local Copy"), QStringLiteral("edit-delete")));
    if (answer != KMessageBox::Continue)
        return;
    QString error;
    if (m_core->unmountFolder(id, &error)) {
        m_unmounting = true;
        refreshButtons();
    } else {
        log(error);
    }
}

void MainWindow::startSync(bool all)
{
    if (m_core->direction() == SyncDirection::Unset) {
        askDirection();
        return;
    }
    const bool started = all ? m_core->syncAll() : m_core->syncFolder(selectedFolderId());
    if (!started)
        log(i18n(
            "The sync could not be started. Another sync (for example the automatic one) may be running."));
}

void MainWindow::syncSelected()
{
    startSync(false);
}

void MainWindow::syncAll()
{
    startSync(true);
}

void MainWindow::applyAutoSync()
{
    m_autoSyncBusy = true;
    refreshButtons();
    m_core->setAutoSync(m_autoSync->isChecked(), m_interval->value());
}

void MainWindow::onProgress(const SyncProgress& progress)
{
    m_progress->setValue(progressPermille(progress));
    m_progressText->setText(formatProgress(progress));
    m_currentFile->setText(progress.currentPath);
}

void MainWindow::onConflicts(const QString& folderId, const QVector<ConflictInfo>& conflicts)
{
    auto* dialog = new ConflictDialog(folderName(folderId), conflicts, this);
    dialog->setObjectName(QStringLiteral("conflictDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(
        dialog, &QDialog::accepted, this, [this, dialog]() { m_core->resolveConflicts(dialog->choices()); });
    connect(dialog, &QDialog::rejected, this, [this]() { m_core->cancelSync(); });
    dialog->open();
}

void MainWindow::onDeletions(const QString& folderId, const QStringList& local, const QStringList& remote)
{
    auto* dialog = new DeletionDialog(folderName(folderId), local, remote, this);
    dialog->setObjectName(QStringLiteral("deletionDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this]() { m_core->confirmDeletions(true); });
    connect(dialog, &QDialog::rejected, this, [this]() { m_core->confirmDeletions(false); });
    dialog->open();
}

void MainWindow::onFolderFinished(const SyncResult& result)
{
    log(QStringLiteral("%1: %2").arg(folderName(result.folderId), formatResult(result)));
    for (const QString& e : result.errors)
        log(i18n("  Error: %1", e));
    for (const QString& s : result.skipped)
        log(i18n("  Skipped: %1", s));
}

void MainWindow::onSyncFinished()
{
    m_progressText->setText(i18n("Idle"));
    m_currentFile->clear();
    refreshButtons();
}

} // namespace gds
