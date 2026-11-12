#include "appcore.h"

#include "format.h"
#include "http.h"
#include "logger.h"
#include "tokensource.h"

#include <KConfigGroup>

#include <QCoreApplication>
#include <QDir>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>

#include <utility>

namespace gds {

namespace {
    /// Runs \p work in the thread pool and \p done back in the thread of \p context.
    template <typename Result, typename Work, typename Done>
    void runInBackground(QObject* context, Work work, Done done)
    {
        auto* watcher = new QFutureWatcher<Result>(context);
        QObject::connect(watcher, &QFutureWatcher<Result>::finished, context, [watcher, done]() {
            const Result result = watcher->result();
            watcher->deleteLater();
            done(result);
        });
        watcher->setFuture(QtConcurrent::run(work));
    }
}

static QString missingSignIn()
{
    return QStringLiteral("The sign-in data is missing from the KDE wallet. Sign out and sign in again.");
}

AppPaths AppPaths::defaults()
{
    AppPaths p;
    p.configFile = QStringLiteral("gdrive-syncrc");
    p.stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/state");
    p.unitDir = Scheduler::defaultUnitDir();
    p.executable = QCoreApplication::applicationFilePath();
    p.logDir = Logger::defaultDirectory();
    return p;
}

AppCore::AppCore(GoogleEndpoints endpoints, SecretStore* secrets, PlacesIntegration* places,
    Notifier* notifier, AppPaths paths, OAuthFlow::BrowserOpener browser,
    Scheduler::SystemctlRunner systemctl, QObject* parent)
    : QObject(parent)
    , m_endpoints(std::move(endpoints))
    , m_secrets(secrets)
    , m_notifier(notifier)
    , m_paths(std::move(paths))
    , m_browser(std::move(browser))
    , m_scheduler(m_paths.unitDir, m_paths.executable, std::move(systemctl))
    , m_config(KSharedConfig::openConfig(m_paths.configFile, KConfig::SimpleConfig))
    , m_configWriter(m_config)
{
    qRegisterMetaType<gds::DriveItem>();
    qRegisterMetaType<QVector<gds::DriveItem>>();
    qRegisterMetaType<QVector<gds::SyncResult>>();
    qRegisterMetaType<GdsConflictChoices>();
    if (m_paths.logDir.isEmpty())
        m_paths.logDir = Logger::defaultDirectory();
    QString logError;
    if (!Logger::instance().setDirectory(logDirectory(), &logError)) {
        // the configured folder is gone or read-only: fall back to the default
        Logger::instance().setDirectory(m_paths.logDir);
        gds::logError(QStringLiteral("Log location %1 cannot be used: %2").arg(logDirectory(), logError));
    }
    m_folders = new FolderManager(m_config, &m_configWriter, m_paths.stateDir, places, this);
    setupLogging();
    connect(m_secrets, &SecretStore::opened, this, &AppCore::onStoreOpened);
    m_releaseTimer.setSingleShot(true);
    m_releaseTimer.setInterval(30000); // covers several clicks in the folder browser
    connect(&m_releaseTimer, &QTimer::timeout, this, &AppCore::closeSecretsIfUnused);
    m_thread.setObjectName(QStringLiteral("gdrive-sync-worker"));
    m_thread.start();
}

AppCore::~AppCore()
{
    m_destroying = true;
    if (m_engine)
        m_engine->requestCancel();
    destroyEngine();
    m_syncLease.reset();
    m_loginLease.reset();
    m_secretRequests.clear();
    m_secrets->close();
    m_thread.quit();
    m_thread.wait();
    // background tasks reference members of this object
    QThreadPool::globalInstance()->waitForDone();
    m_configWriter.flush();
}

QString AppCore::logDirectory() const
{
    const QString chosen = m_config->group("Logging").readEntry("directory", QString());
    return chosen.isEmpty() ? m_paths.logDir : chosen;
}

QString AppCore::logFilePath() const
{
    return Logger::instance().filePath();
}

bool AppCore::setLogDirectory(const QString& directory, QString* error)
{
    const QString target
        = directory.trimmed().isEmpty() ? m_paths.logDir : QDir::cleanPath(directory.trimmed());
    const QString previous = Logger::instance().filePath();
    if (!Logger::instance().setDirectory(target, error)) {
        gds::logError(QStringLiteral("Log location %1 cannot be used").arg(target));
        return false;
    }
    KConfigGroup group = m_config->group("Logging");
    if (target == m_paths.logDir)
        group.deleteEntry("directory");
    else
        group.writeEntry("directory", target);
    m_configWriter.save();
    logInfo(QStringLiteral("Log location changed to %1 (previous log file: %2)")
                .arg(Logger::instance().filePath(), previous.isEmpty() ? QStringLiteral("none") : previous));
    return true;
}

QString AppCore::folderLabel(const QString& folderId) const
{
    const FolderConfig* f = m_folders->find(folderId);
    return f ? QStringLiteral("\"%1\" (%2)").arg(f->driveFolderName, f->localPath) : folderId;
}

/// Writes the application's events to the log file:
/// INFO for sign-in/sign-out and configuration changes, DEBUG for file
/// syncs, WARN for sync errors, ERROR for unexpected application errors.
void AppCore::setupLogging()
{
    connect(this, &AppCore::initialized, this, [this](bool ok, const QString& error) {
        if (!ok)
            gds::logError(error);
        else if (isLoggedIn())
            logInfo(QStringLiteral("Restored the sign-in of %1").arg(accountEmail()));
        else
            logInfo(QStringLiteral("Started; not signed in"));
    });
    connect(this, &AppCore::loginSucceeded, this,
        [this]() { logInfo(QStringLiteral("Signed in as %1").arg(accountEmail())); });
    connect(this, &AppCore::loginFailed, this,
        [](const QString& error) { logWarn(QStringLiteral("Sign-in failed: %1").arg(error)); });
    connect(this, &AppCore::authorizationLost, this,
        []() { logWarn(QStringLiteral("Google rejected the stored sign-in; a new sign-in is required")); });
    connect(this, &AppCore::folderSyncStarted, this, [this](const QString& id) {
        logDebug(QStringLiteral("Sync started: %1, direction %2, %3")
                     .arg(folderLabel(id), directionLabel(direction()),
                         m_batchInteractive ? QStringLiteral("manual") : QStringLiteral("automatic")));
    });
    connect(this, &AppCore::conflictsFound, this,
        [this](const QString& id, const QVector<ConflictInfo>& conflicts) {
            logDebug(QStringLiteral("Sync of %1 asks about %2 conflict(s)")
                         .arg(folderLabel(id))
                         .arg(conflicts.size()));
        });
    connect(this, &AppCore::deletionsPending, this,
        [this](const QString& id, const QStringList& local, const QStringList& remote) {
            logDebug(QStringLiteral("Sync of %1 asks to confirm %2 local and %3 Drive deletion(s)")
                         .arg(folderLabel(id))
                         .arg(local.size())
                         .arg(remote.size()));
        });
    connect(this, &AppCore::folderSyncFinished, this, [this](const SyncResult& r) {
        const QString label = folderLabel(r.folderId);
        for (const QString& e : r.errors)
            logWarn(QStringLiteral("Sync error in %1: %2").arg(label, e));
        for (const QString& s : r.skipped)
            logDebug(QStringLiteral("Skipped in %1: %2").arg(label, s));
        const QString summary = QStringLiteral("Sync finished: %1: %2").arg(label, formatResult(r));
        if (r.status == SyncResult::Failed || r.status == SyncResult::AuthRequired)
            logWarn(summary);
        else
            logDebug(summary);
    });
    connect(
        m_folders, &FolderManager::folderRemoved, this, [](const QString&, bool ok, const QString& error) {
            if (!ok)
                gds::logError(QStringLiteral("Unmount: %1").arg(error));
        });
}

void AppCore::initialize()
{
    // The sign-in state comes from the settings: no wallet access at start.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_initialized = true;
            Q_EMIT initialized(true, QString());
            Q_EMIT accountChanged();
        },
        Qt::QueuedConnection);
}

SecretsLease::SecretsLease(AppCore* core)
    : m_core(core)
{
}

SecretsLease::~SecretsLease()
{
    if (m_core)
        m_core->releaseSecrets();
}

void AppCore::requestSecrets(QObject* context, std::function<void(const Lease&, const QString&)> ready)
{
    ++m_secretUsers;
    m_releaseTimer.stop();
    m_secretRequests.append(
        {QPointer<QObject>(context), std::make_shared<SecretsLease>(this), std::move(ready)});
    if (m_secretsOpening)
        return; // answered by onStoreOpened()
    m_secretsOpening = true;
    m_secrets->open(); // emits opened() also when it is open already
}

void AppCore::onStoreOpened(bool ok)
{
    m_secretsOpening = false;
    const QString error = ok
        ? QString()
        : QStringLiteral("The KDE wallet could not be opened. It is required to read the "
                         "sign-in data; unlock the wallet when it asks for the password.");
    if (ok && m_config->group("Wallet").readEntry("removeSignIn", false)) {
        // the sign-out could not remove the sign-in data from the wallet back then
        if (isLoggedIn() || m_secrets->remove(SecretKeys::refreshToken())) {
            m_config->group("Wallet").deleteEntry("removeSignIn");
            m_configWriter.save();
            if (!isLoggedIn())
                logInfo(
                    QStringLiteral("Removed the sign-in data of the earlier sign-out from the KDE wallet"));
        }
    }
    if (!ok)
        gds::logError(error);
    if (ok && !m_config->group("Client").readEntry("configured", false) && !clientId().isEmpty()) {
        // a client saved before this flag existed
        m_config->group("Client").writeEntry("configured", true);
        m_configWriter.save();
    }
    const QVector<SecretsRequest> requests = std::move(m_secretRequests);
    m_secretRequests.clear();
    for (const SecretsRequest& request : requests) {
        if (request.context)
            request.ready(ok ? request.lease : Lease(), error);
    }
    // leases that nobody kept are released when `requests` goes out of scope
}

void AppCore::releaseSecrets()
{
    if (m_destroying)
        return;
    if (--m_secretUsers == 0)
        m_releaseTimer.start();
}

void AppCore::closeSecretsIfUnused()
{
    if (m_secretUsers > 0 || m_secretsOpening)
        return;
    m_tokens.reset(); // the secrets are not kept in memory while nothing needs them
    if (m_secrets->isOpen())
        logDebug(QStringLiteral("KDE wallet no longer needed; closed it"));
    m_secrets->close();
}

bool AppCore::ensureTokens()
{
    if (m_tokens)
        return true;
    const QString refresh = m_secrets->read(SecretKeys::refreshToken());
    if (refresh.isEmpty() || clientId().isEmpty())
        return false;
    m_tokens = std::make_shared<TokenSource>(m_endpoints, clientId(), clientSecret(), refresh);
    if (!m_accountRefreshed) {
        m_accountRefreshed = true;
        refreshAccountInfo(); // name and picture may have changed since the sign-in
    }
    return true;
}

bool AppCore::hasClientCredentials() const
{
    return m_config->group("Client").readEntry("configured", false) || isLoggedIn();
}

QString AppCore::clientId() const
{
    return m_secrets->read(SecretKeys::clientId());
}

QString AppCore::clientSecret() const
{
    return m_secrets->read(SecretKeys::clientSecret());
}

bool AppCore::setClientCredentials(const QString& clientId, const QString& clientSecret, QString* error)
{
    const QString previousId = this->clientId();
    QString reason;
    bool ok = m_secrets->write(SecretKeys::clientId(), clientId.trimmed());
    if (!ok) {
        reason = m_secrets->lastError();
    } else if (!m_secrets->write(SecretKeys::clientSecret(), clientSecret.trimmed())) {
        ok = false;
        reason = m_secrets->lastError();
        // do not leave a new ID with an old secret behind
        m_secrets->write(SecretKeys::clientId(), previousId);
    }
    if (ok) {
        m_tokens.reset(); // built again with the new client
        m_config->group("Client").writeEntry("configured", true);
        m_configWriter.save();
        logInfo(QStringLiteral("Google API client changed"));
        return true;
    }
    if (reason.isEmpty())
        reason = QStringLiteral("unknown reason");
    const QString message
        = QStringLiteral("The Google API client could not be stored in the KDE wallet: %1. The change was "
                         "aborted: nothing was saved and %2.")
              .arg(reason,
                  previousId.isEmpty()
                      ? QStringLiteral("signing in stays unavailable until the client is saved")
                      : QStringLiteral("the previously saved client stays in effect"));
    gds::logError(message);
    if (error)
        *error = message;
    return false;
}

QString AppCore::accountEmail() const
{
    return m_config->group("Account").readEntry("email", QString());
}

QString AppCore::accountName() const
{
    return m_config->group("Account").readEntry("name", QString());
}

QString AppCore::accountPhotoUrl() const
{
    return m_config->group("Account").readEntry("photo", QString());
}

void AppCore::storeAccountInfo(const AccountInfo& info)
{
    KConfigGroup group = m_config->group("Account");
    group.writeEntry("email", info.email);
    group.writeEntry("name", info.displayName);
    group.writeEntry("photo", info.photoLink);
    m_configWriter.save();
}

/// Updates name and picture of a restored login in the background.
void AppCore::refreshAccountInfo()
{
    if (!m_tokens)
        return;
    const GoogleEndpoints endpoints = m_endpoints;
    const auto tokens = m_tokens;
    runInBackground<DriveResult<AccountInfo>>(
        this,
        [endpoints, tokens]() {
            DriveClient client(endpoints, tokens);
            client.setMaxRetries(1);
            return client.about();
        },
        [this, tokens](const DriveResult<AccountInfo>& about) {
            // ignore the answer when the user signed out or switched accounts meanwhile
            if (!about.ok() || tokens != m_tokens || about.value.email.isEmpty())
                return;
            if (about.value.email == accountEmail() && about.value.displayName == accountName()
                && about.value.photoLink == accountPhotoUrl())
                return;
            storeAccountInfo(about.value);
            Q_EMIT accountChanged();
        });
}

SyncDirection AppCore::direction() const
{
    const int value = m_config->group("Account").readEntry("direction", 0);
    return value >= 1 && value <= 3 ? SyncDirection(value) : SyncDirection::Unset;
}

void AppCore::setDirection(SyncDirection direction)
{
    m_config->group("Account").writeEntry("direction", int(direction));
    m_configWriter.save();
    logInfo(QStringLiteral("Sync direction set to %1").arg(directionLabel(direction)));
    Q_EMIT accountChanged();
}

void AppCore::login()
{
    if (isLoginInProgress())
        return;
    m_loginPending = true;
    // the client is read from the wallet, and the sign-in is stored there
    requestSecrets(this, [this](const Lease& lease, const QString& error) {
        if (!m_loginPending)
            return; // cancelled meanwhile
        m_loginPending = false;
        if (!lease) {
            Q_EMIT loginFailed(error);
            return;
        }
        if (clientId().isEmpty()) {
            logInfo(QStringLiteral("Sign-in needs the Google API client first"));
            Q_EMIT clientCredentialsRequired();
            return;
        }
        logInfo(QStringLiteral("Sign-in started in the web browser"));
        m_loginLease = lease;
        m_flow = new OAuthFlow(m_endpoints, clientId(), clientSecret(), m_browser, this);
        connect(m_flow, &OAuthFlow::succeeded, this, &AppCore::onTokens);
        connect(m_flow, &OAuthFlow::failed, this, [this](const QString& reason) {
            m_flow->deleteLater();
            m_flow = nullptr;
            m_loginLease.reset();
            if (std::exchange(m_loginCancelRequested, false)) {
                logInfo(QStringLiteral("Sign-in cancelled"));
                Q_EMIT loginCancelled();
            } else {
                Q_EMIT loginFailed(reason);
            }
        });
        m_flow->start();
    });
}

void AppCore::cancelLogin()
{
    if (m_flow) {
        m_loginCancelRequested = true;
        m_flow->cancel(); // reported as loginCancelled(), not as a failure
    } else if (m_loginPending) {
        m_loginPending = false;
        logInfo(QStringLiteral("Sign-in cancelled"));
        Q_EMIT loginCancelled();
    }
}

void AppCore::onTokens(const OAuthTokens& tokens)
{
    m_flow->deleteLater();
    m_flow = nullptr;
    const Lease lease = std::exchange(m_loginLease, Lease()); // held until the sign-in is stored

    auto source = std::make_shared<TokenSource>(m_endpoints, clientId(), clientSecret(), tokens.refreshToken);
    source->setAccessToken(tokens.accessToken, tokens.expiresIn);
    const GoogleEndpoints endpoints = m_endpoints;
    // Ask Drive who signed in (blocking call, so in the background).
    runInBackground<DriveResult<AccountInfo>>(
        this,
        [endpoints, source]() {
            DriveClient client(endpoints, source);
            return client.about();
        },
        [this, source, lease](const DriveResult<AccountInfo>& about) {
            if (!about.ok()) {
                Q_EMIT loginFailed(
                    QStringLiteral("Could not read the Google account: %1").arg(about.error.message));
                return;
            }
            if (!m_secrets->write(SecretKeys::refreshToken(), source->refreshToken())) {
                const QString message = QStringLiteral(
                    "The sign-in data could not be stored in the KDE wallet: %1. The sign-in was "
                    "aborted; you are not signed in.")
                                            .arg(m_secrets->lastError());
                gds::logError(message);
                Q_EMIT loginFailed(message);
                return;
            }
            // Signing in again with a different account starts from scratch.
            const QString previous = accountEmail();
            if (!previous.isEmpty() && previous != about.value.email) {
                m_folders->clear();
                m_config->group("Account").deleteEntry("direction");
            }
            m_tokens = source;
            m_accountRefreshed = true;
            m_config->group("Client").writeEntry("configured", true);
            m_config->group("Wallet").deleteEntry("removeSignIn");
            storeAccountInfo(about.value);
            Q_EMIT accountChanged();
            Q_EMIT loginSucceeded();
        });
}

void AppCore::logout()
{
    cancelLogin();
    if (m_syncing) {
        if (m_engine)
            m_engine->requestCancel();
        m_queue.clear();
        finishBatch();
    }

    logInfo(QStringLiteral("Signed out of %1; the folder list and the automatic sync were removed")
                .arg(accountEmail()));
    m_tokens.reset();
    m_folders->clear();
    m_config->deleteGroup("Account");
    m_configWriter.save();

    // Remove the sign-in from the wallet, revoke it at Google and remove the
    // timer, without blocking the GUI.
    requestSecrets(this, [this](const Lease& lease, const QString& error) {
        QString refresh;
        if (lease)
            refresh = m_secrets->read(SecretKeys::refreshToken());
        if (!lease || !m_secrets->remove(SecretKeys::refreshToken())) {
            gds::logError(QStringLiteral(
                "The sign-in data could not be removed from the KDE wallet: %1. You are signed "
                "out; the removal is repeated the next time the wallet is opened.")
                              .arg(lease ? m_secrets->lastError() : error));
            m_config->group("Wallet").writeEntry("removeSignIn", true);
            m_configWriter.save();
        }
        revokeAndStopTimer(refresh);
    });

    Q_EMIT accountChanged();
    Q_EMIT loggedOut();
}

void AppCore::revokeAndStopTimer(const QString& refresh)
{
    const GoogleEndpoints endpoints = m_endpoints;
    Scheduler scheduler = m_scheduler;
    runInBackground<bool>(
        this,
        [endpoints, refresh, scheduler]() mutable {
            if (!refresh.isEmpty()) {
                HttpRequest req;
                req.method = "POST";
                req.url = QUrl(endpoints.revokeUrl);
                req.headers.append(HttpHeader("Content-Type", "application/x-www-form-urlencoded"));
                req.body = formEncode({{QStringLiteral("token"), refresh}});
                req.transferTimeoutMs = 15000;
                QNetworkAccessManager nam;
                httpSend(nam, req);
            }
            return !scheduler.isEnabled() || scheduler.disable();
        },
        [this](bool) { Q_EMIT autoSyncChanged(true, QString()); });
}

void AppCore::createEngine()
{
    m_engine = new SyncEngine(m_endpoints, m_tokens);
    m_engine->setRetryBaseDelayMs(m_retryBaseMs);
    m_engine->moveToThread(&m_thread);
    connect(m_engine, &SyncEngine::progress, this, &AppCore::progress);
    connect(m_engine, &SyncEngine::conflictsFound, this, &AppCore::conflictsFound);
    connect(m_engine, &SyncEngine::deletionsPending, this, &AppCore::deletionsPending);
    connect(m_engine, &SyncEngine::finished, this, &AppCore::onEngineFinished);
}

void AppCore::destroyEngine()
{
    if (!m_engine)
        return;
    m_engine->disconnect(this);
    m_engine->deleteLater(); // runs in the worker thread after the current job returned
    m_engine = nullptr;
}

void AppCore::finishBatch()
{
    m_syncing = false;
    m_currentFolder.clear();
    if (m_lock)
        m_lock->unlock();
    destroyEngine();
    m_syncLease.reset(); // the wallet is not needed any more
    Q_EMIT syncFinished(m_results);
}

void AppCore::failQueuedJobs(SyncResult::Status status, const QString& message)
{
    const QVector<SyncJob> jobs = std::exchange(m_queue, {});
    for (const SyncJob& job : jobs) {
        SyncResult r;
        r.status = status;
        r.folderId = job.folderId;
        r.errorMessage = message;
        m_results.append(r);
        m_folders->recordSync(r.folderId, formatResult(r));
        Q_EMIT folderSyncFinished(r);
    }
    finishBatch();
}

void AppCore::listDriveFolders(const QString& parentId)
{
    if (!isLoggedIn()) {
        Q_EMIT driveFoldersListed(parentId, {}, QStringLiteral("Not signed in"));
        return;
    }
    requestSecrets(this, [this, parentId](const Lease& lease, const QString& error) {
        if (!lease) {
            Q_EMIT driveFoldersListed(parentId, {}, error);
            return;
        }
        if (!ensureTokens()) {
            Q_EMIT driveFoldersListed(parentId, {}, missingSignIn());
            Q_EMIT authorizationLost();
            return;
        }
        const GoogleEndpoints endpoints = m_endpoints;
        const auto tokens = m_tokens;
        runInBackground<DriveResult<QVector<DriveItem>>>(
            this,
            [endpoints, tokens, parentId]() {
                DriveClient client(endpoints, tokens);
                return client.listChildren(parentId, true);
            },
            [this, parentId, tokens, lease](const DriveResult<QVector<DriveItem>>& result) {
                Q_EMIT driveFoldersListed(
                    parentId, result.value, result.ok() ? QString() : result.error.message);
                if (tokens->authorizationLost())
                    Q_EMIT authorizationLost();
            });
    });
}

bool AppCore::unmountFolder(const QString& id, QString* error, bool deleteLocalFiles)
{
    if (m_syncing) {
        if (error)
            *error = QStringLiteral("A sync is running. Wait for it to finish or cancel it first.");
        return false;
    }
    if (!m_folders->find(id)) {
        if (error)
            *error = QStringLiteral("Unknown folder");
        return false;
    }
    m_folders->removeFolder(id, deleteLocalFiles);
    return true;
}

bool AppCore::syncFolder(const QString& id, bool interactive)
{
    const FolderConfig* f = m_folders->find(id);
    return f && startJobs({*f}, interactive);
}

bool AppCore::syncAll(bool interactive)
{
    return startJobs(m_folders->folders(), interactive);
}

bool AppCore::startJobs(const QVector<FolderConfig>& folders, bool interactive)
{
    if (m_syncing || !isLoggedIn() || folders.isEmpty() || direction() == SyncDirection::Unset)
        return false;
    QDir().mkpath(m_paths.stateDir);
    if (!m_lock) {
        m_lock = std::make_unique<QLockFile>(m_paths.stateDir + QStringLiteral("/sync.lock"));
        m_lock->setStaleLockTime(0);
    }
    if (!m_lock->tryLock(0)) {
        // another instance (e.g. the automatic sync) is syncing right now
        logWarn(QStringLiteral("Sync not started: another gdrive-sync process is syncing"));
        return false;
    }
    m_results.clear();
    m_queue.clear();
    for (const FolderConfig& f : folders) {
        SyncJob job;
        job.folderId = f.id;
        job.driveFolderId = f.driveFolderId;
        job.localPath = f.localPath;
        job.stateFile = m_folders->stateFile(f.id);
        job.direction = direction();
        job.interactive = interactive;
        m_queue.append(job);
    }
    m_syncing = true;
    m_batchInteractive = interactive;
    Q_EMIT syncStarted();
    // The sign-in is read from the wallet for the time of the sync.
    const int batch = ++m_batchId;
    requestSecrets(this, [this, batch](const Lease& lease, const QString& error) {
        if (!m_syncing || batch != m_batchId)
            return; // cancelled meanwhile
        if (!lease) {
            failQueuedJobs(SyncResult::Failed, error);
            return;
        }
        if (!ensureTokens()) {
            failQueuedJobs(SyncResult::AuthRequired, missingSignIn());
            Q_EMIT authorizationLost();
            return;
        }
        m_syncLease = lease;
        createEngine();
        startNextJob();
    });
    return true;
}

void AppCore::startNextJob()
{
    if (m_queue.isEmpty()) {
        if (!m_batchInteractive && m_notifier) {
            int deletions = 0, conflicts = 0, failed = 0;
            for (const SyncResult& r : qAsConst(m_results)) {
                deletions += r.pendingDeletions;
                conflicts += r.pendingConflicts;
                if (r.status == SyncResult::Failed || r.status == SyncResult::AuthRequired)
                    ++failed;
            }
            if (deletions > 0 || conflicts > 0) {
                m_notifier->notify(QStringLiteral("syncAttention"), QStringLiteral("Google Drive Sync"),
                    QStringLiteral("The automatic sync skipped %1 deletion(s) and %2 conflict(s). "
                                   "Open Google Drive Sync and run a manual sync to review them.")
                        .arg(deletions)
                        .arg(conflicts));
            }
            if (failed > 0) {
                m_notifier->notify(QStringLiteral("syncFailed"), QStringLiteral("Google Drive Sync"),
                    QStringLiteral("The automatic sync failed for %1 folder(s).").arg(failed));
            }
        }
        finishBatch();
        return;
    }
    const SyncJob job = m_queue.takeFirst();
    m_currentFolder = job.folderId;
    Q_EMIT folderSyncStarted(job.folderId);
    QMetaObject::invokeMethod(
        m_engine, [engine = m_engine, job]() { engine->run(job); }, Qt::QueuedConnection);
}

void AppCore::onEngineFinished(const SyncResult& result)
{
    m_results.append(result);
    m_folders->recordSync(result.folderId, formatResult(result));
    Q_EMIT folderSyncFinished(result);
    if (result.status == SyncResult::AuthRequired) {
        m_queue.clear();
        if (m_tokens && m_tokens->authorizationLost())
            Q_EMIT authorizationLost();
    }
    startNextJob();
}

void AppCore::cancelSync()
{
    if (!m_syncing)
        return;
    logDebug(QStringLiteral("Sync cancel requested"));
    if (m_engine)
        m_engine->requestCancel();
    else
        failQueuedJobs(SyncResult::Cancelled, QStringLiteral("Cancelled")); // still waiting for the wallet
}

void AppCore::resolveConflicts(const QHash<QString, ConflictChoice>& choices)
{
    if (m_engine)
        m_engine->provideConflictChoices(choices);
}

void AppCore::confirmDeletions(bool proceed)
{
    logDebug(proceed ? QStringLiteral("Deletions confirmed")
                     : QStringLiteral("Deletions declined: sync cancelled"));
    if (m_engine)
        m_engine->provideDeletionDecision(proceed);
}

bool AppCore::autoSyncEnabled() const
{
    return m_scheduler.isEnabled();
}

int AppCore::autoSyncIntervalMinutes() const
{
    return m_scheduler.intervalMinutes();
}

void AppCore::setAutoSync(bool enabled, int intervalMinutes)
{
    Scheduler scheduler = m_scheduler;
    // systemctl can take a while: run it in the background
    runInBackground<QString>(
        this,
        [scheduler, enabled, intervalMinutes]() mutable {
            QString error;
            if (enabled)
                scheduler.enable(intervalMinutes, &error);
            else
                scheduler.disable(&error);
            return error;
        },
        [this, enabled, intervalMinutes](const QString& error) {
            if (!error.isEmpty())
                gds::logError(QStringLiteral("Automatic sync could not be changed: %1").arg(error));
            else if (enabled)
                logInfo(QStringLiteral("Automatic sync enabled, every %1 minutes").arg(intervalMinutes));
            else
                logInfo(QStringLiteral("Automatic sync disabled"));
            Q_EMIT autoSyncChanged(error.isEmpty(), error);
        });
}

} // namespace gds
