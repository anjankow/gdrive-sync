// Requirement: "The user can successfully login, logout and login with
// another account" and "securely store the authentication data".
#include "pkce.h"
#include "testutil.h"

#include <QDirIterator>
#include <QRegularExpression>
#include <QUrlQuery>

using namespace gds;
using namespace gdstest;

class TestAccount : public QObject {
    Q_OBJECT

    static bool login(Fixture& f)
    {
        QSignalSpy ok(f.core.get(), &AppCore::loginSucceeded);
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        const bool success = ok.wait(10000);
        if (!success && !failed.isEmpty())
            qWarning() << "login failed:" << failed.first().at(0).toString();
        return success;
    }

    static void prepare(Fixture& f)
    {
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.setClient(QStringLiteral("my-client-id"), QStringLiteral("my-client-secret")));
    }

    /// True when \p needle appears in any file below \p dir.
    static bool appearsOnDisk(const QString& dir, const QString& needle)
    {
        QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (readFile(it.next()).contains(needle.toUtf8()))
                return true;
        }
        return false;
    }

private Q_SLOTS:
    void pkceVerifierAndChallengeFollowRfc7636()
    {
        const QString verifier = Pkce::generateVerifier();
        QVERIFY(verifier.size() >= 43 && verifier.size() <= 128);
        QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z0-9._~-]+$")).match(verifier).hasMatch());
        QVERIFY(verifier != Pkce::generateVerifier());
        // test vector from RFC 7636, appendix B
        QCOMPARE(Pkce::challengeS256(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")),
            QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void loginRequiresClientCredentials()
    {
        Fixture f;
        f.createCore();
        QVERIFY(f.initialize());
        QSignalSpy required(f.core.get(), &AppCore::clientCredentialsRequired);
        f.core->login();
        QTRY_COMPARE(required.count(), 1);
        QVERIFY(!f.core->isLoggedIn());
        QVERIFY(!f.core->isLoginInProgress());
        QVERIFY(f.browser.opened.isEmpty());
    }

    void loginUsesBrowserLoopbackAndPkce()
    {
        Fixture f;
        prepare(f);
        f.server.setAccountEmail(QStringLiteral("alice@example.com"));
        QVERIFY(login(f));
        QVERIFY(f.core->isLoggedIn());
        QCOMPARE(f.core->accountEmail(), QStringLiteral("alice@example.com"));
        QCOMPARE(f.core->accountName(), QStringLiteral("Test User"));
        QCOMPARE(f.core->accountPhotoUrl(), f.server.baseUrl() + QStringLiteral("/photo"));

        // the authorization request sent to the browser
        QCOMPARE(f.browser.opened.size(), 1);
        const auto q = f.server.lastAuthorizationQuery();
        QCOMPARE(q.value(QStringLiteral("client_id")), QStringLiteral("my-client-id"));
        QCOMPARE(q.value(QStringLiteral("response_type")), QStringLiteral("code"));
        QCOMPARE(q.value(QStringLiteral("scope")), QStringLiteral("https://www.googleapis.com/auth/drive"));
        QCOMPARE(q.value(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QVERIFY(!q.value(QStringLiteral("code_challenge")).isEmpty());
        QVERIFY(!q.value(QStringLiteral("state")).isEmpty());
        QVERIFY(QRegularExpression(QStringLiteral("^http://127\\.0\\.0\\.1:\\d+$"))
                    .match(q.value(QStringLiteral("redirect_uri")))
                    .hasMatch());

        // the code exchange (the mock verifies the PKCE verifier against the challenge)
        const auto t = f.server.lastTokenRequest();
        QCOMPARE(t.value(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
        QCOMPARE(t.value(QStringLiteral("redirect_uri")), q.value(QStringLiteral("redirect_uri")));
        QCOMPARE(Pkce::challengeS256(t.value(QStringLiteral("code_verifier"))),
            q.value(QStringLiteral("code_challenge")));
        QCOMPARE(t.value(QStringLiteral("client_secret")), QStringLiteral("my-client-secret"));
    }

    void browserIsSentToTheProjectPageAfterSignIn()
    {
        QCOMPARE(GoogleEndpoints().signedInUrl, QStringLiteral("https://github.com/anjankow/gdrive-sync"));
        Fixture f;
        prepare(f);
        QVERIFY(login(f));
        // the browser gets an answer from the local redirect address and ends on the project page
        QTRY_COMPARE(f.browser.finished, 1);
        QCOMPARE(f.browser.finalError, QString());
        QCOMPARE(f.browser.finalStatus, 200);
        QCOMPARE(f.browser.redirects.size(), 2);
        QCOMPARE(f.browser.redirects.last(), QUrl(f.server.baseUrl() + QStringLiteral("/signed-in")));
    }

    void secretsAreStoredOnlyInTheWallet()
    {
        Fixture f;
        prepare(f);
        QVERIFY(login(f));
        f.core->setDefaultDirection(SyncDirection::TwoWay);
        const QString refresh = f.server.issuedRefreshTokens().last();
        QCOMPARE(f.secrets.stored(SecretKeys::refreshToken()), refresh);
        QCOMPARE(f.secrets.stored(SecretKeys::clientSecret()), QStringLiteral("my-client-secret"));
        // nothing secret in the config or state files
        f.core->flushSettings();
        QVERIFY(QFile::exists(f.paths().configFile));
        QVERIFY(!appearsOnDisk(f.tmp.path(), refresh));
        QVERIFY(!appearsOnDisk(f.tmp.path(), QStringLiteral("my-client-secret")));
        QVERIFY(!appearsOnDisk(f.tmp.path(), QStringLiteral("at-id")));
    }

    void deniedConsentFailsLogin()
    {
        Fixture f;
        prepare(f);
        f.server.setDenyAuthorization(true);
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        QVERIFY(failed.wait(10000));
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("access_denied")));
        QVERIFY(!f.core->isLoggedIn());
        QVERIFY(f.secrets.stored(SecretKeys::refreshToken()).isEmpty());
    }

    void forgedRedirectWithWrongStateIsRejected()
    {
        Fixture f;
        prepare(f);
        f.browser.refuse = false;
        // a "browser" that does not follow the redirect but forges the callback
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        QNetworkAccessManager nam;
        f.core.reset();
        f.core = std::make_unique<AppCore>(GoogleEndpoints::forTestServer(f.server.baseUrl()), &f.secrets,
            &f.places, &f.notifier, f.paths(), [&](const QUrl& url) {
                const QString redirect
                    = QUrlQuery(url).queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded);
                nam.get(QNetworkRequest(QUrl(redirect + QStringLiteral("/?code=stolen&state=wrong"))));
                return true;
            });
        QSignalSpy init(f.core.get(), &AppCore::initialized);
        f.core->initialize();
        QVERIFY(init.count() > 0 || init.wait());
        QSignalSpy failed2(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        QVERIFY(failed2.wait(10000));
        QVERIFY(!f.core->isLoggedIn());
        QCOMPARE(f.server.requestCount(QStringLiteral("POST /token")), 0);
    }

    void loginCanBeCancelled()
    {
        Fixture f;
        prepare(f);
        f.browser.refuse = false;
        f.core.reset();
        f.createCore();
        QVERIFY(f.initialize());
        // browser never comes back
        f.core.reset();
        f.core = std::make_unique<AppCore>(GoogleEndpoints::forTestServer(f.server.baseUrl()), &f.secrets,
            &f.places, &f.notifier, f.paths(), [](const QUrl&) { return true; });
        QSignalSpy init(f.core.get(), &AppCore::initialized);
        f.core->initialize();
        QVERIFY(init.count() > 0 || init.wait());
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        QSignalSpy cancelled(f.core.get(), &AppCore::loginCancelled);
        // while the wallet is still being opened
        f.core->login();
        QVERIFY(f.core->isLoginInProgress());
        f.core->cancelLogin();
        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!f.core->isLoginInProgress());
        QVERIFY(!f.core->isLoggedIn());
        QTest::qWait(100);

        // while the browser shows the Google sign-in page
        cancelled.clear();
        f.core->login();
        QTRY_VERIFY(f.logText().contains(QStringLiteral("Sign-in started in the web browser")));
        QVERIFY(f.core->isLoginInProgress());
        f.core->cancelLogin();
        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!f.core->isLoginInProgress());
        QVERIFY(!f.core->isLoggedIn());
        QCOMPARE(failed.count(), 0); // cancelling is not a failure
    }

    void loginSurvivesRestart()
    {
        Fixture f;
        prepare(f);
        f.server.setAccountEmail(QStringLiteral("alice@example.com"));
        QVERIFY(login(f));
        f.core->setDefaultDirection(SyncDirection::DriveToLocal);
        const int browserVisits = f.browser.opened.size();

        // "reboot": a new application instance with the same wallet and config
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(f.core->isLoggedIn());
        QCOMPARE(f.core->accountEmail(), QStringLiteral("alice@example.com"));
        QCOMPARE(f.core->defaultDirection(), SyncDirection::DriveToLocal);
        QCOMPARE(f.browser.opened.size(), browserVisits); // no new sign-in needed

        // and the restored session really works against Drive
        f.server.addFolder(QStringLiteral("Docs"));
        QSignalSpy listed(f.core.get(), &AppCore::driveFoldersListed);
        f.core->listDriveFolders(QStringLiteral("root"));
        QVERIFY(listed.wait(10000));
        QCOMPARE(listed.first().at(2).toString(), QString());
        QCOMPARE(listed.first().at(1).value<QVector<DriveItem>>().size(), 1);
    }

    void logoutRevokesAndClearsEverything()
    {
        Fixture f;
        prepare(f);
        QVERIFY(login(f));
        f.core->setDefaultDirection(SyncDirection::LocalToDrive);
        const QString folder = f.server.addFolder(QStringLiteral("Docs"));
        QVERIFY(!f.mount(folder, QStringLiteral("Docs")).isEmpty());
        const QString refresh = f.secrets.stored(SecretKeys::refreshToken());
        QVERIFY(!refresh.isEmpty());

        QSignalSpy loggedOut(f.core.get(), &AppCore::loggedOut);
        f.core->logout();
        QCOMPARE(loggedOut.count(), 1);
        QVERIFY(!f.core->isLoggedIn());
        QVERIFY(f.core->accountEmail().isEmpty());
        // the default direction is a setting of the application, not of the account
        QCOMPARE(f.core->defaultDirection(), SyncDirection::LocalToDrive);
        QVERIFY(f.core->folders()->folders().isEmpty());
        QVERIFY(f.places.places.isEmpty());
        QTRY_VERIFY(f.secrets.stored(SecretKeys::refreshToken()).isEmpty());
        QTRY_VERIFY(f.server.revokedTokens().contains(refresh));
        // the OAuth client configuration is not account data and is kept
        QCOMPARE(f.secrets.stored(SecretKeys::clientId()), QStringLiteral("my-client-id"));
        QVERIFY(f.core->hasClientCredentials());

        // still logged out after a restart
        f.createCore();
        QVERIFY(f.initialize());
        QVERIFY(!f.core->isLoggedIn());
    }

    void loginWithAnotherAccountAfterLogout()
    {
        Fixture f;
        prepare(f);
        f.server.setAccountEmail(QStringLiteral("alice@example.com"));
        QVERIFY(login(f));
        const QString firstToken = f.secrets.stored(SecretKeys::refreshToken());
        f.core->logout();

        f.server.setAccountEmail(QStringLiteral("bob@example.com"));
        QVERIFY(login(f));
        QVERIFY(f.core->isLoggedIn());
        QCOMPARE(f.core->accountEmail(), QStringLiteral("bob@example.com"));
        QVERIFY(f.secrets.stored(SecretKeys::refreshToken()) != firstToken);
        // the consent/account chooser is always requested so another account can be picked
        QVERIFY(f.server.lastAuthorizationQuery()
                    .value(QStringLiteral("prompt"))
                    .contains(QStringLiteral("select_account")));
    }

    void unavailableWalletIsReportedWhenNeeded()
    {
        Fixture f;
        f.secrets.failOpen = true;
        f.createCore();
        QVERIFY(f.initialize()); // starting needs no wallet
        QCOMPARE(f.secrets.openCount, 0);
        QSignalSpy failed(f.core.get(), &AppCore::loginFailed);
        f.core->login();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("KDE wallet could not be opened")));
        QVERIFY(!f.core->isLoginInProgress());
    }
};

QTEST_GUILESS_MAIN(TestAccount)
#include "tst_account.moc"
