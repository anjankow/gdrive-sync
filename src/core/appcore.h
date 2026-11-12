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
#include <QTimer>

#include <functional>
#include <memory>

namespace gds {

class AppCore;

/// Keeps the secret store open while an action needs it. The store is closed
/// again shortly after the last lease is gone.
class SecretsLease {
public:
    explicit SecretsLease(AppCore* core);
    ~SecretsLease();
    SecretsLease(const SecretsLease&) = delete;
    SecretsLease& operator=(const SecretsLease&) = delete;

private:
    QPointer<AppCore> m_core;
};
using Lease = std::shared_ptr<SecretsLease>;

struct AppPaths {
    QString configFile; ///< KConfig file (no secrets are stored in it)
    QString stateDir; ///< per-folder sync state
    QString unitDir; ///< systemd user unit directory
    QString executable; ///< binary started by the automatic sync
    QString logDir; ///< default folder of the log file

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

    /// Restores a persisted login from the settings. Emits initialized().
    /// The KDE wallet is not opened here: it is opened only for an action that
    /// needs it (sync, Drive folder browser, sign-in/out, Google API client)
    /// and closed again afterwards.
    void initialize();
    /// Waits until the settings saved so far are written to disk (they are written in the background).
    void flushSettings() { m_configWriter.flush(); }
    bool isInitialized() const { return m_initialized; }

    /// Opens the secret store and calls \p ready with a lease that keeps it open
    /// (or with no lease and the reason when it could not be opened). \p ready
    /// is not called when \p context is gone by then.
    void requestSecrets(
        QObject* context, std::function<void(const Lease& lease, const QString& error)> ready);
    /// How long the store stays open after the last lease is gone.
    void setSecretsLingerMs(int ms) { m_releaseTimer.setInterval(ms); }

    // OAuth client of the user's own Google Cloud project (kept in the secret store)
    /// Whether a client is known to be saved (from the settings; no wallet
    /// access). login() checks the wallet itself.
    bool hasClientCredentials() const;
    /// Read from the secret store: only valid while a lease is held.
    QString clientId() const;
    QString clientSecret() const;
    /// Stores the client in the wallet (needs a lease). On failure nothing is
    /// changed (the previous client stays in effect) and \p error says what
    /// happened and why.
    bool setClientCredentials(const QString& clientId, const QString& clientSecret, QString* error = nullptr);

    // account
    bool isLoggedIn() const { return !accountEmail().isEmpty(); }
    QString accountEmail() const;
    QString accountName() const;
    /// URL of the profile picture of the signed-in user; may be empty.
    QString accountPhotoUrl() const;
    bool isLoginInProgress() const { return m_flow != nullptr || m_loginPending; }
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

    // log file
    /// Folder that holds gdrive-sync.log (the user's choice or the default below /tmp).
    QString logDirectory() const;
    QString defaultLogDirectory() const { return m_paths.logDir; }
    QString logFilePath() const;
    /// Moves the log to another folder; an empty path selects the default.
    bool setLogDirectory(const QString& directory, QString* error = nullptr);

    void setRetryBaseDelayMs(int ms) { m_retryBaseMs = ms; }

Q_SIGNALS:
    void initialized(bool ok, const QString& error);
    void loginSucceeded();
    void loginFailed(const QString& error);
    /// The user cancelled the sign-in (cancelLogin()); not a failure.
    void loginCancelled();
    /// login() found no Google API client in the wallet: it has to be entered first.
    void clientCredentialsRequired();
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
    friend class SecretsLease;
    void releaseSecrets();
    void closeSecretsIfUnused();
    void onStoreOpened(bool ok);
    /// Builds the token source from the wallet (a lease must be held).
    bool ensureTokens();
    void failQueuedJobs(SyncResult::Status status, const QString& message);
    void setupLogging();
    QString folderLabel(const QString& folderId) const;
    void onTokens(const OAuthTokens& tokens);
    void revokeAndStopTimer(const QString& refreshToken);
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
    ConfigWriter m_configWriter;
    FolderManager* m_folders = nullptr;
    bool m_initialized = false;

    QPointer<OAuthFlow> m_flow;
    bool m_loginPending = false; ///< sign-in waits for the wallet
    bool m_loginCancelRequested = false;
    Lease m_loginLease;
    /// Only while a lease is held: the secrets are not kept in memory otherwise.
    std::shared_ptr<TokenSource> m_tokens;
    bool m_accountRefreshed = false;

    struct SecretsRequest {
        QPointer<QObject> context;
        Lease lease;
        std::function<void(const Lease&, const QString&)> ready;
    };
    QVector<SecretsRequest> m_secretRequests;
    int m_secretUsers = 0;
    bool m_secretsOpening = false;
    bool m_destroying = false;
    QTimer m_releaseTimer;

    QThread m_thread;
    SyncEngine* m_engine = nullptr;
    QVector<SyncJob> m_queue;
    QVector<SyncResult> m_results;
    bool m_syncing = false;
    bool m_batchInteractive = true;
    QString m_currentFolder;
    int m_batchId = 0;
    Lease m_syncLease;
    /// Prevents the GUI and an automatic run from syncing at the same time.
    std::unique_ptr<QLockFile> m_lock;
    int m_retryBaseMs = 500;
};

} // namespace gds

Q_DECLARE_METATYPE(gds::DriveItem)
Q_DECLARE_METATYPE(QVector<gds::DriveItem>)
Q_DECLARE_METATYPE(QVector<gds::SyncResult>)
