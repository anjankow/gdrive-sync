#include "mainwindow.h"

#include "dialogs.h"
#include "format.h"

#include <KLocalizedString>
#include <KMessageBox>
#include <KWindowSystem>
#include <KX11Extras>

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTime>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

namespace gds {

namespace {
    /// The mouse wheel scrolls the folder list; it must not change a direction by accident.
    class DirectionCombo : public QComboBox {
    public:
        using QComboBox::QComboBox;

    protected:
        void wheelEvent(QWheelEvent* event) override { event->ignore(); }
    };
}

namespace {
    enum { FolderIdRole = Qt::UserRole + 1 };
    const int kAvatarSize = 32;

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
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(i18nc("@title:window", "Google Drive Sync"));
    openFolder = [](const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); };

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    // ---- account row: status on the left, sign-in / profile picture and settings on the right
    auto* accountRow = new QHBoxLayout;
    auto* accountLabels = new QVBoxLayout;
    m_account = new QLabel(central);
    m_account->setObjectName(QStringLiteral("accountLabel"));
    accountLabels->addWidget(m_account);
    accountRow->addLayout(accountLabels, 1);

    m_login = button(QStringLiteral("im-user"), i18n("Sign In"), "loginButton", central);
    accountRow->addWidget(m_login);

    // shown instead of the sign-in button while signed in
    m_accountButton = new QToolButton(central);
    m_accountButton->setObjectName(QStringLiteral("accountButton"));
    m_accountButton->setPopupMode(QToolButton::InstantPopup);
    m_accountButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_accountButton->setAutoRaise(true);
    m_accountButton->setIconSize(QSize(kAvatarSize, kAvatarSize));
    m_accountButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    auto* accountMenu = new QMenu(m_accountButton);
    m_accountInfo = new QLabel(accountMenu);
    m_accountInfo->setObjectName(QStringLiteral("accountMenuInfo"));
    m_accountInfo->setTextFormat(Qt::RichText);
    m_accountInfo->setContentsMargins(12, 8, 12, 8);
    auto* infoAction = new QWidgetAction(accountMenu);
    infoAction->setDefaultWidget(m_accountInfo);
    accountMenu->addAction(infoAction);
    accountMenu->addSeparator();
    m_signOut = accountMenu->addAction(QIcon::fromTheme(QStringLiteral("system-log-out")), i18n("Sign Out"));
    m_signOut->setObjectName(QStringLiteral("actionSignOut"));
    m_accountButton->setMenu(accountMenu);
    accountRow->addWidget(m_accountButton);

    m_settings = new QToolButton(central);
    m_settings->setObjectName(QStringLiteral("settingsButton"));
    m_settings->setText(i18n("Settings"));
    m_settings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    m_settings->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settings->setPopupMode(QToolButton::InstantPopup);
    auto* settingsMenu = new QMenu(m_settings);
    m_apiClient = settingsMenu->addAction(
        QIcon::fromTheme(QStringLiteral("dialog-password")), i18n("Google API Client"));
    m_apiClient->setObjectName(QStringLiteral("actionApiClient"));
    m_autoSyncAction
        = settingsMenu->addAction(QIcon::fromTheme(QStringLiteral("chronometer")), i18n("Automatic Sync"));
    m_autoSyncAction->setObjectName(QStringLiteral("actionAutoSync"));
    m_defaultDirection = settingsMenu->addAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Default Sync Direction"));
    m_defaultDirection->setObjectName(QStringLiteral("actionDefaultDirection"));
    m_logLocation
        = settingsMenu->addAction(QIcon::fromTheme(QStringLiteral("text-x-log")), i18n("Log Location"));
    m_logLocation->setObjectName(QStringLiteral("actionLogLocation"));
    m_settings->setMenu(settingsMenu);
    accountRow->addWidget(m_settings);
    layout->addLayout(accountRow);

    // ---- folders: the main area of the window
    auto* foldersBox = new QGroupBox(i18n("Synced Folders"), central);
    foldersBox->setObjectName(QStringLiteral("foldersBox"));
    auto* foldersLayout = new QVBoxLayout(foldersBox);
    m_folders = new QTreeWidget(foldersBox);
    m_folders->setObjectName(QStringLiteral("folderList"));
    m_folders->setRootIsDecorated(false);
    m_folders->setHeaderLabels({i18n("Google Drive folder"), i18n("Local folder"), i18n("Direction"),
        i18n("Last sync"), i18n("Status")});
    m_folders->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_folders->header()->setStretchLastSection(true);
    m_folders->setMinimumHeight(220);
    foldersLayout->addWidget(m_folders);
    auto* folderButtons = new QHBoxLayout;
    m_add = button(QStringLiteral("folder-new"), i18n("Add Folder"), "addButton", foldersBox);
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
    layout->addWidget(foldersBox, 1);

    // ---- progress: one slim bar and one line of text
    auto* progressArea = new QWidget(central);
    progressArea->setObjectName(QStringLiteral("progressArea"));
    progressArea->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* progressLayout = new QVBoxLayout(progressArea);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(2);
    auto* barRow = new QHBoxLayout;
    m_progress = new QProgressBar(progressArea);
    m_progress->setObjectName(QStringLiteral("progressBar"));
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(10);
    m_cancel = button(QStringLiteral("process-stop"), i18n("Cancel"), "cancelButton", progressArea);
    barRow->addWidget(m_progress, 1);
    barRow->addWidget(m_cancel);
    progressLayout->addLayout(barRow);
    auto* textRow = new QHBoxLayout;
    m_progressText = new QLabel(i18n("Idle"), progressArea);
    m_progressText->setObjectName(QStringLiteral("progressText"));
    m_currentFile = new QLabel(progressArea);
    m_currentFile->setObjectName(QStringLiteral("currentFile"));
    m_currentFile->setTextFormat(Qt::PlainText);
    m_currentFile->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // long file names must not widen the window
    m_currentFile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textRow->addWidget(m_progressText);
    textRow->addWidget(m_currentFile, 1);
    progressLayout->addLayout(textRow);
    layout->addWidget(progressArea);

    // ---- log
    m_log = new QPlainTextEdit(central);
    m_log->setObjectName(QStringLiteral("log"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setFixedHeight(110);
    layout->addWidget(m_log);

    setCentralWidget(central);
    resize(900, 680);

    connect(m_login, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(m_signOut, &QAction::triggered, this, &MainWindow::confirmSignOut);
    connect(m_apiClient, &QAction::triggered, this, &MainWindow::editCredentials);
    connect(m_autoSyncAction, &QAction::triggered, this, &MainWindow::editAutoSync);
    connect(m_logLocation, &QAction::triggered, this, &MainWindow::editLogLocation);
    connect(m_defaultDirection, &QAction::triggered, this, &MainWindow::editDefaultDirection);
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
    connect(m_folders, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshButtons);

    connect(m_core, &AppCore::initialized, this, &MainWindow::onInitialized);
    connect(m_core, &AppCore::accountChanged, this, &MainWindow::refreshAccount);
    connect(m_core, &AppCore::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_core, &AppCore::loginFailed, this, [this](const QString& error) {
        refreshAccount();
        log(i18n("Sign-in failed: %1", error), LogLevel::Warn);
        showError(i18nc("@title:window", "Sign-in Failed"), error);
    });
    connect(m_core, &AppCore::loggedOut, this, [this]() { log(i18n("Signed out.")); });
    connect(m_core, &AppCore::loginCancelled, this, [this]() {
        // the user's own choice: no message box
        refreshAccount();
        log(i18n("Sign-in cancelled."));
    });
    connect(m_core, &AppCore::clientCredentialsRequired, this, [this]() {
        refreshAccount();
        log(i18n("Enter the Google API client of your Google Cloud project, then sign in."));
        editCredentials();
    });
    connect(m_core, &AppCore::authorizationLost, this, [this]() {
        log(i18n("Google rejected the stored sign-in. Please sign in again."), LogLevel::Warn);
        refreshAccount();
    });
    connect(m_core->folders(), &FolderManager::foldersChanged, this, &MainWindow::refreshFolders);
    connect(m_core->folders(), &FolderManager::folderRemoved, this,
        [this](const QString&, bool ok, const QString& error) {
            m_unmounting = false;
            log(ok ? (m_unmountKeepsFiles ? i18n("Folder unmounted; the local files were kept.")
                                          : i18n("Folder unmounted; the local copy was deleted."))
                   : error,
                ok ? LogLevel::Info : LogLevel::Error);
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
            log(i18n("Automatic sync could not be changed: %1", error), LogLevel::Error);
            showError(i18nc("@title:window", "Automatic Sync"), error);
        } else if (m_core->isLoggedIn()) {
            log(m_core->autoSyncEnabled()
                    ? i18n("Automatic sync is on (every %1 minutes).", m_core->autoSyncIntervalMinutes())
                    : i18n("Automatic sync is off."));
        }
        refreshButtons();
    });

    refreshAccount();
    refreshFolders();
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

/// Shows an error box once control is back in the event loop. Opening a
/// modal box directly from a signal of a dialog that is just closing would
/// run a nested event loop while that dialog is being torn down.
void MainWindow::showError(const QString& title, const QString& text)
{
    QMetaObject::invokeMethod(
        this, [this, title, text]() { KMessageBox::error(this, text, title, KMessageBox::Notify); },
        Qt::QueuedConnection);
}

void MainWindow::log(const QString& text, LogLevel level)
{
    // every line carries its severity, like the log file
    m_log->appendPlainText(QStringLiteral("[%1] %2 %3")
                               .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                   Logger::levelName(level).leftJustified(5), text));
}

void MainWindow::refreshAccount()
{
    const bool loggedIn = m_core->isLoggedIn();
    if (!m_core->isInitialized()) {
        m_account->setText(i18n("Opening the KDE wallet…"));
    } else if (m_core->isLoginInProgress()) {
        m_account->setText(i18n("Waiting for the sign-in in your web browser…"));
    } else if (loggedIn) {
        // who is signed in is shown by the profile picture and its menu
        m_account->clear();
    } else {
        m_account->setText(i18n("Not signed in"));
    }
    m_login->setText(m_core->isLoginInProgress() ? i18n("Cancel Sign-in") : i18n("Sign In"));
    m_account->setVisible(!m_account->text().isEmpty());

    // signed in: the profile picture replaces the sign-in button
    m_login->setVisible(!loggedIn);
    m_accountButton->setVisible(loggedIn);
    if (loggedIn) {
        const QString name = m_core->accountName();
        m_accountInfo->setText(name.isEmpty()
                ? m_core->accountEmail().toHtmlEscaped()
                : QStringLiteral("<b>%1</b><br>%2")
                      .arg(name.toHtmlEscaped(), m_core->accountEmail().toHtmlEscaped()));
        m_accountButton->setToolTip(name.isEmpty() ? m_core->accountEmail() : name);
        loadAvatar();
    } else {
        m_avatarUrl.clear();
        m_accountInfo->clear();
    }
    refreshButtons();
}

/// Fetches the profile picture without blocking; a generic icon is shown meanwhile.
void MainWindow::loadAvatar()
{
    const QString url = m_core->accountPhotoUrl();
    if (url == m_avatarUrl && !m_accountButton->icon().isNull())
        return;
    m_avatarUrl = url;
    m_accountButton->setIcon(
        QIcon::fromTheme(QStringLiteral("user-identity"), QIcon::fromTheme(QStringLiteral("im-user"))));
    m_accountButton->setProperty("hasProfilePicture", false);
    if (url.isEmpty())
        return;
    QNetworkRequest request {QUrl(url)};
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        QPixmap picture;
        if (url != m_avatarUrl || reply->error() != QNetworkReply::NoError
            || !picture.loadFromData(reply->readAll()))
            return; // keep the generic icon
        setAvatar(picture);
    });
}

void MainWindow::setAvatar(const QPixmap& picture)
{
    // round picture, as in Google's own applications
    const int size = kAvatarSize * 2; // sharp on high-DPI screens
    QPixmap round(size, size);
    round.fill(Qt::transparent);
    QPainter painter(&round);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(0, 0, size, size);
    painter.setClipPath(circle);
    painter.drawPixmap(
        0, 0, picture.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    painter.end();
    m_accountButton->setIcon(QIcon(round));
    m_accountButton->setProperty("hasProfilePicture", true);
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
            3, f.lastSync.isValid() ? QLocale().toString(f.lastSync, QLocale::ShortFormat) : i18n("Never"));
        item->setText(4, f.lastStatus);

        // the direction can be changed at any time; it applies from the next sync
        auto* combo = new DirectionCombo(m_folders);
        combo->setObjectName(QStringLiteral("directionCombo"));
        combo->setProperty("folderId", f.id);
        for (SyncDirection d :
            {SyncDirection::TwoWay, SyncDirection::DriveToLocal, SyncDirection::LocalToDrive})
            combo->addItem(directionLabel(d), int(d));
        combo->setCurrentIndex(combo->findData(int(f.direction)));
        const QString id = f.id;
        // queued: the change rebuilds this list, and with it the combo box that reports it
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, id, combo](int index) {
            const auto direction = SyncDirection(combo->itemData(index).toInt());
            QMetaObject::invokeMethod(
                this, [this, id, direction]() { changeFolderDirection(id, direction); },
                Qt::QueuedConnection);
        });
        m_folders->setItemWidget(item, 2, combo);
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
    m_settings->setEnabled(ready);
    // the client cannot change under a running login session
    m_apiClient->setEnabled(ready && !loggedIn && !m_core->isLoginInProgress());
    m_apiClient->setToolTip(loggedIn ? i18n("Sign out to change the Google API client.") : QString());
    m_autoSyncAction->setEnabled(loggedIn && !m_autoSyncBusy);
    m_add->setEnabled(idle);
    m_unmount->setEnabled(idle && hasSelection);
    m_open->setEnabled(hasSelection);
    m_sync->setEnabled(idle && hasSelection);
    m_syncAll->setEnabled(idle && hasFolders);
    m_cancel->setEnabled(syncing);
    // not while its folder may be syncing
    const auto combos = m_folders->findChildren<QComboBox*>(QStringLiteral("directionCombo"));
    for (QComboBox* combo : combos)
        combo->setEnabled(idle);
}

void MainWindow::changeFolderDirection(const QString& id, SyncDirection direction)
{
    QString error;
    if (m_core->setFolderDirection(id, direction, &error)) {
        log(i18n("Sync direction of \"%1\": %2", folderName(id), directionLabel(direction)));
    } else {
        log(error, LogLevel::Warn);
        refreshFolders(); // show the unchanged direction again
    }
}

void MainWindow::onInitialized(bool ok, const QString& error)
{
    refreshAccount();
    if (!ok) {
        m_account->setText(i18n("KDE wallet not available"));
        m_account->setVisible(true);
        log(error, LogLevel::Error);
        showError(i18nc("@title:window", "KDE Wallet"), error);
    }
}

void MainWindow::editCredentials()
{
    // the saved client is shown from the wallet, and the change is stored there
    m_core->requestSecrets(this, [this](const Lease& lease, const QString& error) {
        if (!lease) {
            log(error, LogLevel::Error);
            showError(i18nc("@title:window", "Google API Client"), error);
            return;
        }
        showCredentialsDialog(lease);
    });
}

void MainWindow::showCredentialsDialog(const Lease& lease)
{
    auto* dialog = new CredentialsDialog(m_core->clientId(), m_core->clientSecret(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // the wallet stays open until the dialog is closed
    connect(dialog, &QObject::destroyed, this, [lease]() {});
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        QString error;
        if (!m_core->setClientCredentials(dialog->clientId(), dialog->clientSecret(), &error)) {
            log(error, LogLevel::Error);
            showError(i18nc("@title:window", "Google API Client"), error);
        } else {
            log(i18n("Google API client saved in the KDE wallet."));
        }
        refreshButtons();
    });
    dialog->open();
}

void MainWindow::onLoginClicked()
{
    if (m_core->isLoginInProgress()) {
        m_core->cancelLogin();
    } else if (!m_core->isLoggedIn()) {
        // straight to the browser; the client settings open only when the wallet has no client
        m_core->login();
        log(i18n("Complete the sign-in in your web browser."));
    }
    refreshAccount();
}

void MainWindow::confirmSignOut()
{
    if (!m_core->isLoggedIn())
        return;
    const auto answer = KMessageBox::warningContinueCancel(this,
        i18n("Sign out of %1?\n\nThe stored sign-in is revoked and the list of synced folders and the "
             "automatic sync are removed. Files already on this computer are kept.",
            m_core->accountEmail()),
        i18nc("@title:window", "Sign Out"), KGuiItem(i18n("Sign Out"), QStringLiteral("system-log-out")));
    if (answer == KMessageBox::Continue)
        m_core->logout();
    refreshAccount();
}

void MainWindow::onLoginSucceeded()
{
    log(i18n("Signed in as %1.", m_core->accountEmail()));
    refreshAccount();
    bringToFront();
}

void MainWindow::bringToFront()
{
    // The sign-in happened in the browser, which holds the focus now.
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    if (KWindowSystem::isPlatformX11())
        KX11Extras::forceActiveWindow(winId()); // overrides the focus stealing prevention
    else if (windowHandle())
        KWindowSystem::activateWindow(windowHandle());
}

void MainWindow::editDefaultDirection()
{
    auto* dialog = new DirectionDialog(this);
    dialog->setObjectName(QStringLiteral("directionDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setDirection(m_core->defaultDirection());
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        m_core->setDefaultDirection(dialog->direction());
        log(i18n("Default sync direction for new folders: %1", directionLabel(dialog->direction())));
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
            showError(i18nc("@title:window", "Add Folder"), error);
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
    bool deleteLocalFiles = true;
    if (f->initiallyNonEmpty) {
        // the folder held the user's own files before it was added: let them choose
        const auto answer = KMessageBox::questionTwoActionsCancel(this,
            i18n("Unmount \"%1\"?\n\nThe folder %2 already held files when it was added. Keep all files in "
                 "it, or delete all of them? Nothing is deleted from Google Drive.",
                f->driveFolderName, f->localPath),
            i18nc("@title:window", "Unmount Folder"),
            KGuiItem(i18n("Unmount and Keep Local Files"), QStringLiteral("folder")),
            KGuiItem(i18n("Unmount and Delete All Local Files"), QStringLiteral("edit-delete")),
            KStandardGuiItem::cancel());
        if (answer == KMessageBox::Cancel)
            return;
        deleteLocalFiles = answer == KMessageBox::SecondaryAction;
    } else {
        const auto answer = KMessageBox::warningContinueCancel(this,
            i18n("Unmount \"%1\"?\n\nThe local copy in %2 will be deleted. Nothing is deleted from Google "
                 "Drive.",
                f->driveFolderName, f->localPath),
            i18nc("@title:window", "Unmount Folder"),
            KGuiItem(i18n("Unmount and Delete Local Copy"), QStringLiteral("edit-delete")));
        if (answer != KMessageBox::Continue)
            return;
    }
    QString error;
    if (m_core->unmountFolder(id, &error, deleteLocalFiles)) {
        m_unmountKeepsFiles = !deleteLocalFiles;
        m_unmounting = true;
        refreshButtons();
    } else {
        log(error, LogLevel::Warn);
    }
}

void MainWindow::startSync(bool all)
{
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

void MainWindow::editAutoSync()
{
    auto* dialog = new AutoSyncDialog(m_core->autoSyncEnabled(), m_core->autoSyncIntervalMinutes(), this);
    dialog->setObjectName(QStringLiteral("autoSyncDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        m_autoSyncBusy = true;
        refreshButtons();
        m_core->setAutoSync(dialog->isEnabledChecked(), dialog->intervalMinutes());
    });
    dialog->open();
}

void MainWindow::editLogLocation()
{
    auto* dialog = new LogLocationDialog(m_core->logDirectory(), m_core->defaultLogDirectory(), this);
    dialog->setObjectName(QStringLiteral("logLocationDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog]() {
        QString error;
        if (m_core->setLogDirectory(dialog->directory(), &error)) {
            log(i18n("Log file: %1", m_core->logFilePath()));
        } else {
            log(i18n("The log location could not be changed: %1", error), LogLevel::Error);
            showError(i18nc("@title:window", "Log Location"), error);
        }
    });
    dialog->open();
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
    const bool failed = result.status == SyncResult::Failed || result.status == SyncResult::AuthRequired;
    log(QStringLiteral("%1: %2").arg(folderName(result.folderId), formatResult(result)),
        failed ? LogLevel::Warn : LogLevel::Info);
    for (const QString& e : result.errors)
        log(i18n("  Error: %1", e), LogLevel::Warn);
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
