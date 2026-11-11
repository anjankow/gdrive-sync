#include "appcore.h"

#include "format.h"
#include "http.h"
#include "tokensource.h"

#include <KConfigGroup>

#include <QCoreApplication>
#include <QDir>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>

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

AppPaths AppPaths::defaults()
{
    AppPaths p;
    p.configFile = QStringLiteral("gdrive-syncrc");
    p.stateDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/state");
    p.unitDir = Scheduler::defaultUnitDir();
    p.executable = QCoreApplication::applicationFilePath();
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
{
    qRegisterMetaType<gds::DriveItem>();
    qRegisterMetaType<QVector<gds::DriveItem>>();
    qRegisterMetaType<QVector<gds::SyncResult>>();
    qRegisterMetaType<GdsConflictChoices>();
    m_folders = new FolderManager(m_config, m_paths.stateDir, places, this);
    connect(m_secrets, &SecretStore::opened, this, &AppCore::onStoreOpened);
    m_thread.setObjectName(QStringLiteral("gdrive-sync-worker"));
    m_thread.start();
}

AppCore::~AppCore()
{
    if (m_engine)
        m_engine->requestCancel();
    destroyEngine();
    m_thread.quit();
    m_thread.wait();
    // background tasks reference members of this object
    QThreadPool::globalInstance()->waitForDone();
}

void AppCore::initialize()
{
    m_secrets->open();
}

void AppCore::onStoreOpened(bool ok)
{
    if (m_initialized)
        return;
    if (!ok) {
        Q_EMIT initialized(false,
            QStringLiteral(
                "The KDE wallet could not be opened. It is required to store the sign-in data securely."));
        return;
    }
    m_initialized = true;
    const QString refresh = m_secrets->read(SecretKeys::refreshToken());
    if (!refresh.isEmpty() && hasClientCredentials()) {
        m_tokens = std::make_shared<TokenSource>(m_endpoints, clientId(), clientSecret(), refresh);
        createEngine();
    }
    Q_EMIT initialized(true, QString());
    Q_EMIT accountChanged();
}

bool AppCore::hasClientCredentials() const
{
    return !clientId().isEmpty();
}

QString AppCore::clientId() const
{
    return m_secrets->read(SecretKeys::clientId());
}

QString AppCore::clientSecret() const
{
    return m_secrets->read(SecretKeys::clientSecret());
}

bool AppCore::setClientCredentials(const QString& clientId, const QString& clientSecret)
{
    return m_secrets->write(SecretKeys::clientId(), clientId.trimmed())
        && m_secrets->write(SecretKeys::clientSecret(), clientSecret.trimmed());
}

QString AppCore::accountEmail() const
{
    return m_config->group("Account").readEntry("email", QString());
}

SyncDirection AppCore::direction() const
{
    const int value = m_config->group("Account").readEntry("direction", 0);
    return value >= 1 && value <= 3 ? SyncDirection(value) : SyncDirection::Unset;
}

void AppCore::setDirection(SyncDirection direction)
{
    m_config->group("Account").writeEntry("direction", int(direction));
    m_config->sync();
    Q_EMIT accountChanged();
}

void AppCore::login()
{
    if (m_flow)
        return;
    if (!m_initialized) {
        Q_EMIT loginFailed(QStringLiteral("The KDE wallet is not available."));
        return;
    }
    if (!hasClientCredentials()) {
        Q_EMIT loginFailed(QStringLiteral("Enter the OAuth client ID of your Google Cloud project first."));
        return;
    }
    m_flow = new OAuthFlow(m_endpoints, clientId(), clientSecret(), m_browser, this);
    connect(m_flow, &OAuthFlow::succeeded, this, &AppCore::onTokens);
    connect(m_flow, &OAuthFlow::failed, this, [this](const QString& error) {
        m_flow->deleteLater();
        m_flow = nullptr;
        Q_EMIT loginFailed(error);
    });
    m_flow->start();
}

void AppCore::cancelLogin()
{
    if (m_flow)
        m_flow->cancel();
}

void AppCore::onTokens(const OAuthTokens& tokens)
{
    m_flow->deleteLater();
    m_flow = nullptr;

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
        [this, source](const DriveResult<AccountInfo>& about) {
            if (!about.ok()) {
                Q_EMIT loginFailed(
                    QStringLiteral("Could not read the Google account: %1").arg(about.error.message));
                return;
            }
            if (!m_secrets->write(SecretKeys::refreshToken(), source->refreshToken())) {
                Q_EMIT loginFailed(QStringLiteral("Could not store the sign-in data in the KDE wallet."));
                return;
            }
            // Signing in again with a different account starts from scratch.
            const QString previous = accountEmail();
            if (!previous.isEmpty() && previous != about.value.email) {
                m_folders->clear();
                m_config->group("Account").deleteEntry("direction");
            }
            if (m_engine)
                m_engine->requestCancel();
            destroyEngine();
            m_tokens = source;
            createEngine();
            m_config->group("Account").writeEntry("email", about.value.email);
            m_config->sync();
            Q_EMIT accountChanged();
            Q_EMIT loginSucceeded();
        });
}

void AppCore::logout()
{
    if (m_flow)
        m_flow->cancel();
    if (m_engine)
        m_engine->requestCancel();
    destroyEngine();
    m_queue.clear();

    const QString refresh = m_secrets->read(SecretKeys::refreshToken());
    m_secrets->remove(SecretKeys::refreshToken());
    m_tokens.reset();
    m_folders->clear();
    m_config->deleteGroup("Account");
    m_config->sync();

    // Revoke the grant at Google and remove the timer without blocking the GUI.
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

    Q_EMIT accountChanged();
    Q_EMIT loggedOut();
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
    if (m_syncing) {
        m_queue.clear();
        finishBatch();
    }
}

void AppCore::finishBatch()
{
    m_syncing = false;
    m_currentFolder.clear();
    if (m_lock)
        m_lock->unlock();
    Q_EMIT syncFinished(m_results);
}

void AppCore::listDriveFolders(const QString& parentId)
{
    if (!m_tokens) {
        Q_EMIT driveFoldersListed(parentId, {}, QStringLiteral("Not signed in"));
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
        [this, parentId, tokens](const DriveResult<QVector<DriveItem>>& result) {
            Q_EMIT driveFoldersListed(parentId, result.value, result.ok() ? QString() : result.error.message);
            if (tokens->authorizationLost())
                Q_EMIT authorizationLost();
        });
}

bool AppCore::unmountFolder(const QString& id, QString* error)
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
    m_folders->removeFolder(id);
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
    if (m_syncing || !m_engine || folders.isEmpty() || direction() == SyncDirection::Unset)
        return false;
    QDir().mkpath(m_paths.stateDir);
    if (!m_lock) {
        m_lock = std::make_unique<QLockFile>(m_paths.stateDir + QStringLiteral("/sync.lock"));
        m_lock->setStaleLockTime(0);
    }
    if (!m_lock->tryLock(0))
        return false; // another instance (e.g. the automatic sync) is syncing right now
    m_engine->resetCancel();
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
    startNextJob();
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
    if (m_engine && m_syncing)
        m_engine->requestCancel();
}

void AppCore::resolveConflicts(const QHash<QString, ConflictChoice>& choices)
{
    if (m_engine)
        m_engine->provideConflictChoices(choices);
}

void AppCore::confirmDeletions(bool proceed)
{
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
        [this](const QString& error) { Q_EMIT autoSyncChanged(error.isEmpty(), error); });
}

} // namespace gds
