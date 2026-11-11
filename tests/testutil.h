#pragma once

#include "appcore.h"
#include "mock/mockgoogleserver.h"
#include "tokensource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace gdstest {

inline bool writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(content) == content.size();
}

inline QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// Makes the file look modified later than any previously recorded mtime.
inline void touchLater(const QString& path, int seconds = 5)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite))
        f.setFileTime(QDateTime::currentDateTime().addSecs(seconds), QFileDevice::FileModificationTime);
}

class FakePlaces : public gds::PlacesIntegration {
public:
    void addPlace(const QString& localPath, const QString& label) override
    {
        places.insert(localPath, label);
    }
    void removePlace(const QString& localPath) override { places.remove(localPath); }
    QMap<QString, QString> places;
};

class FakeNotifier : public gds::Notifier {
public:
    void notify(const QString& eventId, const QString&, const QString& text) override
    {
        events.append(eventId);
        texts.append(text);
    }
    QStringList events;
    QStringList texts;
};

/// Plays the role of the user's web browser: requests the authorization URL
/// and follows the redirect back to the application's loopback listener.
class FakeBrowser : public QObject {
public:
    bool open(const QUrl& url)
    {
        opened.append(url);
        if (refuse)
            return false;
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = nam.get(req);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        return true;
    }
    QNetworkAccessManager nam;
    QList<QUrl> opened;
    bool refuse = false;
};

/// A complete application core wired to the mock server and fakes.
struct Fixture {
    QTemporaryDir tmp;
    MockGoogleServer server;
    gds::MemorySecretStore secrets;
    FakePlaces places;
    FakeNotifier notifier;
    FakeBrowser browser;
    QStringList systemctlCalls;
    bool systemctlFails = false;
    std::unique_ptr<gds::AppCore> core;

    // answers given to the interactive questions of the engine
    bool confirmDeletions = true;
    gds::ConflictChoice conflictChoice = gds::ConflictChoice::Skip;
    QStringList askedLocalDeletions;
    QStringList askedRemoteDeletions;
    QVector<gds::ConflictInfo> askedConflicts;
    int deletionQuestions = 0;
    QVector<gds::SyncProgress> progressEvents;

    gds::AppPaths paths() const
    {
        gds::AppPaths p;
        p.configFile = tmp.path() + QStringLiteral("/config/gdrive-syncrc");
        p.stateDir = tmp.path() + QStringLiteral("/state");
        p.unitDir = tmp.path() + QStringLiteral("/systemd/user");
        p.executable = QStringLiteral("/usr/bin/gdrive-sync");
        return p;
    }

    void createCore()
    {
        core.reset();
        core = std::make_unique<gds::AppCore>(
            gds::GoogleEndpoints::forTestServer(server.baseUrl()), &secrets, &places, &notifier, paths(),
            [this](const QUrl& url) { return browser.open(url); },
            [this](const QStringList& args, QString* error) {
                systemctlCalls.append(args.join(QLatin1Char(' ')));
                if (systemctlFails && error)
                    *error = QStringLiteral("systemctl failed");
                return !systemctlFails;
            });
        core->setRetryBaseDelayMs(5);
        QObject::connect(core.get(), &gds::AppCore::deletionsPending, core.get(),
            [this](const QString&, const QStringList& local, const QStringList& remote) {
                ++deletionQuestions;
                askedLocalDeletions = local;
                askedRemoteDeletions = remote;
                core->confirmDeletions(confirmDeletions);
            });
        QObject::connect(core.get(), &gds::AppCore::conflictsFound, core.get(),
            [this](const QString&, const QVector<gds::ConflictInfo>& conflicts) {
                askedConflicts = conflicts;
                QHash<QString, gds::ConflictChoice> choices;
                for (const auto& c : conflicts)
                    choices.insert(c.path, conflictChoice);
                core->resolveConflicts(choices);
            });
        QObject::connect(core.get(), &gds::AppCore::progress, core.get(),
            [this](const gds::SyncProgress& p) { progressEvents.append(p); });
    }

    bool initialize()
    {
        QSignalSpy spy(core.get(), &gds::AppCore::initialized);
        core->initialize();
        return (spy.count() > 0 || spy.wait(5000)) && spy.first().at(0).toBool();
    }

    /// Starts the core already signed in (refresh token present in the wallet).
    bool startLoggedIn(gds::SyncDirection direction = gds::SyncDirection::TwoWay)
    {
        secrets.open();
        QCoreApplication::processEvents();
        secrets.write(gds::SecretKeys::clientId(), QStringLiteral("test-client"));
        secrets.write(gds::SecretKeys::clientSecret(), QStringLiteral("test-secret"));
        secrets.write(gds::SecretKeys::refreshToken(), server.issueRefreshToken());
        createCore();
        if (!initialize() || !core->isLoggedIn())
            return false;
        core->setDirection(direction);
        return true;
    }

    QString localDir(const QString& name) const { return tmp.path() + QStringLiteral("/local/") + name; }

    /// Mounts a Drive folder into <tmp>/local/<name>; returns the folder id.
    QString mount(const QString& driveFolderId, const QString& name)
    {
        QString error;
        const QString id = core->folders()->addFolder(driveFolderId, name, localDir(name), &error);
        if (id.isEmpty())
            qWarning() << "mount failed:" << error;
        return id;
    }

    QVector<gds::SyncResult> waitForSync(int timeoutMs = 30000)
    {
        QSignalSpy spy(core.get(), &gds::AppCore::syncFinished);
        if (!spy.wait(timeoutMs))
            return {};
        return spy.first().at(0).value<QVector<gds::SyncResult>>();
    }

    gds::SyncResult sync(const QString& folderId, bool interactive = true)
    {
        progressEvents.clear();
        QSignalSpy spy(core.get(), &gds::AppCore::syncFinished);
        gds::SyncResult failed;
        failed.status = gds::SyncResult::Failed;
        failed.errorMessage = QStringLiteral("sync did not start/finish");
        if (!core->syncFolder(folderId, interactive))
            return failed;
        if (spy.isEmpty() && !spy.wait(30000))
            return failed;
        const auto results = spy.first().at(0).value<QVector<gds::SyncResult>>();
        return results.isEmpty() ? failed : results.first();
    }
};

} // namespace gdstest
