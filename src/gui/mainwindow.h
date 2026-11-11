#pragma once

#include "appcore.h"

#include <QMainWindow>
#include <QPointer>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
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
    void refreshAutoSync();
    void log(const QString& text);
    QString folderName(const QString& id) const;

    void onInitialized(bool ok, const QString& error);
    void onLoginClicked();
    void onLoginSucceeded();
    void editCredentials();
    void askDirection();
    void addFolder();
    void unmountSelected();
    void syncSelected();
    void syncAll();
    void applyAutoSync();
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
    QPushButton* m_credentials;
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
    QCheckBox* m_autoSync;
    QSpinBox* m_interval;
    QPushButton* m_applyAuto;
    QPlainTextEdit* m_log;
    bool m_unmounting = false;
    bool m_autoSyncBusy = false;
};

} // namespace gds
