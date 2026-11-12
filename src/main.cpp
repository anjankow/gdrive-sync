#include "appcore.h"
#include "kdeintegration.h"
#include "kwalletsecretstore.h"
#include "logger.h"
#include "mainwindow.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QNetworkProxyFactory>
#include <QTimer>

#include <cstring>

using namespace gds;

namespace {

/// "gdrive-sync --sync": what the systemd timer runs. Syncs all folders
/// without asking anything; deletions and conflicts are left pending.
int runUnattended(QApplication& app)
{
    KWalletSecretStore secrets;
    KdeNotifier notifier;
    AppCore core(GoogleEndpoints(), &secrets, nullptr, &notifier, AppPaths::defaults(),
        [](const QUrl&) { return false; });
    logInfo(QStringLiteral("gdrive-sync %1 started for an automatic sync").arg(QStringLiteral(GDS_VERSION)));
    int exitCode = 0;
    // leave a moment for notifications to be delivered before exiting
    const auto quitSoon = [&app]() { QTimer::singleShot(1500, &app, &QCoreApplication::quit); };

    QObject::connect(&core, &AppCore::initialized, &app, [&](bool ok, const QString&) {
        if (!ok || !core.isLoggedIn()) {
            exitCode = ok ? 0 : 1; // not signed in: nothing to do
            quitSoon();
            return;
        }
        if (!core.syncAll(/*interactive=*/false))
            quitSoon(); // no folders, no direction or another sync is running
    });
    QObject::connect(&core, &AppCore::syncFinished, &app, [&](const QVector<SyncResult>& results) {
        for (const SyncResult& r : results) {
            if (r.status == SyncResult::Failed || r.status == SyncResult::AuthRequired)
                exitCode = 1;
        }
        quitSoon();
    });
    core.initialize();
    app.exec();
    return exitCode;
}

} // namespace

int main(int argc, char** argv)
{
    // first: plugins may already print while the application is created
    installQtMessageRouting();
    bool unattended = false;
    for (int i = 1; i < argc; ++i)
        unattended = unattended || std::strcmp(argv[i], "--sync") == 0;
    // The timer may fire without a graphical session; no window is needed then.
    if (unattended && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("gdrive-sync");
    KAboutData about(QStringLiteral("gdrive-sync"), i18n("Google Drive Sync"), QStringLiteral(GDS_VERSION),
        i18n("Sync Google Drive folders with local directories"), KAboutLicense::Custom);
    about.setLicenseText(QStringLiteral("MIT License. See the LICENSE file of the project."));
    about.setDesktopFileName(QStringLiteral("io.github.anjankow.gdrivesync"));
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(
        QStringLiteral("io.github.anjankow.gdrivesync"), QIcon(QStringLiteral(":/icons/gdrive-sync.png"))));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addOption(
        {QStringLiteral("sync"), i18n("Sync all folders once without user interaction and exit.")});
    parser.process(app);
    about.processCommandLine(&parser);

    QNetworkProxyFactory::setUseSystemConfiguration(true);

    if (parser.isSet(QStringLiteral("sync")))
        return runUnattended(app);

    KDBusService service(KDBusService::Unique);

    KWalletSecretStore secrets;
    KdePlaces places;
    KdeNotifier notifier;
    AppCore core(GoogleEndpoints(), &secrets, &places, &notifier, AppPaths::defaults(),
        [](const QUrl& url) { return QDesktopServices::openUrl(url); });
    logInfo(QStringLiteral("gdrive-sync %1 started").arg(QStringLiteral(GDS_VERSION)));
    MainWindow window(&core);
    QObject::connect(&service, &KDBusService::activateRequested, &window, [&window]() {
        window.show();
        window.raise();
        window.activateWindow();
    });
    window.show();
    secrets.setWindowId(window.winId());
    core.initialize();
    return app.exec();
}
