#include "kdeintegration.h"

#include "hostenvironment.h"

#include <KApplicationTrader>
#include <KFilePlacesModel>
#include <KIO/DesktopExecParser>
#include <KNotification>

#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QUrl>

namespace gds {

KdePlaces::KdePlaces() = default;
KdePlaces::~KdePlaces() = default;

KFilePlacesModel* KdePlaces::model()
{
    if (!m_model)
        m_model = std::make_unique<KFilePlacesModel>();
    return m_model.get();
}

bool KdePlaces::hasPlace(const QString& localPath)
{
    const QUrl url = QUrl::fromLocalFile(localPath);
    KFilePlacesModel* m = model();
    for (int row = 0; row < m->rowCount(); ++row) {
        if (m->url(m->index(row, 0)).matches(url, QUrl::StripTrailingSlash))
            return true;
    }
    return false;
}

void KdePlaces::addPlace(const QString& localPath, const QString& label)
{
    if (hasPlace(localPath))
        return;
    model()->addPlace(label, QUrl::fromLocalFile(localPath), QStringLiteral("folder-cloud"), QString());
}

void KdePlaces::removePlace(const QString& localPath)
{
    const QUrl url = QUrl::fromLocalFile(localPath);
    KFilePlacesModel* m = model();
    for (int row = m->rowCount() - 1; row >= 0; --row) {
        const QModelIndex index = m->index(row, 0);
        if (m->url(index).matches(url, QUrl::StripTrailingSlash))
            m->removePlace(index);
    }
}

bool ExternalPrograms::start(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(hostEnvironment());
    process.setWorkingDirectory(QDir::homePath());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    return process.startDetached();
}

bool ExternalPrograms::openFolder(const QString& localPath, KService::Ptr service)
{
    const QUrl url = QUrl::fromLocalFile(localPath);
    if (!service)
        service = KApplicationTrader::preferredService(QStringLiteral("inode/directory"));
    if (service) {
        KIO::DesktopExecParser parser(*service, {url});
        QStringList args = parser.resultingArguments();
        if (!args.isEmpty() && start(args.takeFirst(), args))
            return true;
    }
    return QDesktopServices::openUrl(url); // no file manager found: leave it to the desktop
}

bool ExternalPrograms::openUrl(const QUrl& url)
{
    return start(QStringLiteral("xdg-open"), {url.toString(QUrl::FullyEncoded)})
        || QDesktopServices::openUrl(url);
}

void KdeNotifier::notify(const QString& eventId, const QString& title, const QString& text)
{
    KNotification::event(eventId, title, text, QStringLiteral("io.github.anjankow.gdrivesync"), nullptr,
        KNotification::CloseOnTimeout, QStringLiteral("gdrive-sync"));
}

} // namespace gds
