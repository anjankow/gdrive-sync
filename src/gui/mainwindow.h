#pragma once

#include "appcore.h"
#include "logger.h"

#include <QMainWindow>
#include <QPointer>

class QAction;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;
class QToolButton;
class QTreeWidget;

namespace gds {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppCore* core, QWidget* parent = nullptr);

    /// Open file-manager URLs through this hook (replaced in tests).
    std::function<void(const QString& localPath)> openFolder;

    QString selectedFolderId() const;
    void selectFolder(const QString& id);

private:
    void refreshAccount();
    void refreshFolders();
    void refreshButtons();
    void loadAvatar();
    void setAvatar(const QPixmap& picture);
    void log(const QString& text, LogLevel level = LogLevel::Info);
    void showError(const QString& title, const QString& text);
    QString folderName(const QString& id) const;

    void onInitialized(bool ok, const QString& error);
    void onLoginClicked();
    void onLoginSucceeded();
    void editCredentials();
    void askDirection();
    void bringToFront();
    void showCredentialsDialog(const gds::Lease& lease);
    void addFolder();
    void unmountSelected();
    void syncSelected();
    void syncAll();
    void editAutoSync();
    void editLogLocation();
    void confirmSignOut();
    void onProgress(const gds::SyncProgress& progress);
    void onConflicts(const QString& folderId, const QVector<gds::ConflictInfo>& conflicts);
    void onDeletions(const QString& folderId, const QStringList& local, const QStringList& remote);
    void onFolderFinished(const gds::SyncResult& result);
    void onSyncFinished();
    void startSync(bool all);

    AppCore* m_core;
    QLabel* m_account;
    QLabel* m_direction;
    QPushButton* m_login;
    QToolButton* m_accountButton;
    QLabel* m_accountInfo;
    QAction* m_signOut;
    QToolButton* m_settings;
    QAction* m_apiClient;
    QAction* m_autoSyncAction;
    QAction* m_logLocation;
    QNetworkAccessManager* m_nam;
    QString m_avatarUrl;
    QTreeWidget* m_folders;
    QPushButton* m_add;
    QPushButton* m_unmount;
    QPushButton* m_open;
    QPushButton* m_sync;
    QPushButton* m_syncAll;
    QPushButton* m_cancel;
    QProgressBar* m_progress;
    QLabel* m_progressText;
    QLabel* m_currentFile;
    QPlainTextEdit* m_log;
    bool m_unmounting = false;
    bool m_autoSyncBusy = false;
};

} // namespace gds
