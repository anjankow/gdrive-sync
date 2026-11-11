#pragma once

#include "integration.h"

#include <memory>

class KFilePlacesModel;

namespace gds {

/// Adds/removes entries in the "Places" panel shared by Dolphin and the
/// KDE file dialogs.
class KdePlaces : public PlacesIntegration {
public:
    KdePlaces();
    ~KdePlaces() override;
    void addPlace(const QString& localPath, const QString& label) override;
    void removePlace(const QString& localPath) override;
    bool hasPlace(const QString& localPath);

private:
    KFilePlacesModel* model();
    std::unique_ptr<KFilePlacesModel> m_model;
};

class KdeNotifier : public Notifier {
public:
    void notify(const QString& eventId, const QString& title, const QString& text) override;
};

} // namespace gds
