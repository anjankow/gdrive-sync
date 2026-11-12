#pragma once

#include "driveclient.h"
#include "endpoints.h"
#include "foldermanager.h"
#include "integration.h"
#include "oauthflow.h"
#include "scheduler.h"
#include "secretstore.h"
#include "syncengine.h"
#include "synctypes.h"

#include <KSharedConfig>

#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <memory>

namespace gds {

struct AppPaths {
    QString configFile; ///< KConfig file (no secrets are stored in it)
    QString stateDir; ///< per-folder sync state
    QString unitDir; ///< systemd user unit directory
    QString executable; ///< binary started by the automatic sync

    static AppPaths defaults();
};

/// Application logic without any widgets: account, folders, sync and
/// automatic sync. Lives in the GUI thread and never blocks it: network,
/// file and process work happens in worker threads.
class AppCore : public QObject {
    Q_OBJECT
public:
    AppCore(GoogleEndpoints endpoints, SecretStore* secrets, PlacesIntegration* places, Notifier* notifier,
        AppPaths paths, OAuthFlow::BrowserOpener browser, Scheduler::SystemctlRunner systemctl = {},
        QObject* parent = nullptr);
    ~AppCore() override;

    /// Opens the secret store and restores a persisted login. Emits initialized().
    void initialize();
    bool isInitialized() const { return m_initialized; }

    // OAuth client of the user's own Google Cloud project (kept in the secret store)
    bool hasClientCredentials() const;
    QString clientId() const;
    QString clientSecret() const;
    bool setClientCredentials(const QString& clientId, const QString& clientSecret);

    // account
    bool isLoggedIn() const { return m_tokens != nullptr; }
    QString accountEmail() const;
    QString accountName() const;
    /// URL of the profile picture of the signed-in user; may be empty.
    QString accountPhotoUrl() const;
    bool isLoginInProgress() const { return m_flow != nullptr; }
    void login();
    void cancelLogin();
    void logout();

    /// Chosen once after login; applies to all folders until logout.
    SyncDirection direction() const;
    void setDirection(SyncDirection direction);

    // folders
    FolderManager* folders() const { return m_folders; }
    /// Lists the sub-folders of a Drive folder ("root" = My Drive) in the background.
    void listDriveFolders(const QString& parentId);
    /// Unmounts a folder and deletes its local copy. Refused while it is syncing.
    bool unmountFolder(const QString& id, QString* error = nullptr);

    // sync
    bool isSyncing() const { return m_syncing; }
    bool syncFolder(const QString& id, bool interactive = true);
    bool syncAll(bool interactive = true);
    void cancelSync();
    void resolveConflicts(const QHash<QString, gds::ConflictChoice>& choices);
    void confirmDeletions(bool proceed);

    // automatic sync (systemd user timer)
    bool autoSyncEnabled() const;
    int autoSyncIntervalMinutes() const;
    void setAutoSync(bool enabled, int intervalMinutes);

    void setRetryBaseDelayMs(int ms) { m_retryBaseMs = ms; }
    std::shared_ptr<TokenSource> tokenSource() const { return m_tokens; }

Q_SIGNALS:
    void initialized(bool ok, const QString& error);
    void loginSucceeded();
    void loginFailed(const QString& error);
    void loggedOut();
    void accountChanged();
    void authorizationLost();
    void driveFoldersListed(
        const QString& parentId, const QVector<gds::DriveItem>& folders, const QString& error);
    void syncStarted();
    void folderSyncStarted(const QString& folderId);
    void progress(const gds::SyncProgress& progress);
    void conflictsFound(const QString& folderId, const QVector<gds::ConflictInfo>& conflicts);
    void deletionsPending(
        const QString& folderId, const QStringList& localPaths, const QStringList& remotePaths);
    void folderSyncFinished(const gds::SyncResult& result);
    void syncFinished(const QVector<gds::SyncResult>& results);
    void autoSyncChanged(bool ok, const QString& error);

private:
    void onStoreOpened(bool ok);
    void onTokens(const OAuthTokens& tokens);
    void storeAccountInfo(const AccountInfo& info);
    void refreshAccountInfo();
    void createEngine();
    void destroyEngine();
    bool startJobs(const QVector<FolderConfig>& folders, bool interactive);
    void startNextJob();
    void finishBatch();
    void onEngineFinished(const SyncResult& result);

    GoogleEndpoints m_endpoints;
    SecretStore* m_secrets;
    Notifier* m_notifier;
    AppPaths m_paths;
    OAuthFlow::BrowserOpener m_browser;
    Scheduler m_scheduler;
    KSharedConfig::Ptr m_config;
    FolderManager* m_folders = nullptr;
    bool m_initialized = false;

    QPointer<OAuthFlow> m_flow;
    std::shared_ptr<TokenSource> m_tokens;

    QThread m_thread;
    SyncEngine* m_engine = nullptr;
    QVector<SyncJob> m_queue;
    QVector<SyncResult> m_results;
    bool m_syncing = false;
    bool m_batchInteractive = true;
    QString m_currentFolder;
    /// Prevents the GUI and an automatic run from syncing at the same time.
    std::unique_ptr<QLockFile> m_lock;
    int m_retryBaseMs = 500;
};

} // namespace gds

Q_DECLARE_METATYPE(gds::DriveItem)
Q_DECLARE_METATYPE(QVector<gds::DriveItem>)
Q_DECLARE_METATYPE(QVector<gds::SyncResult>)
