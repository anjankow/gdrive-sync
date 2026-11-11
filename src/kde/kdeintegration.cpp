#include "kdeintegration.h"

#include <KFilePlacesModel>
#include <KNotification>

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

void KdeNotifier::notify(const QString& eventId, const QString& title, const QString& text)
{
    KNotification::event(eventId, title, text, QStringLiteral("folder-cloud"), nullptr,
        KNotification::CloseOnTimeout, QStringLiteral("gdrive-sync"));
}

} // namespace gds
