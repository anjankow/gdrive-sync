#pragma once

#include <QString>

namespace gds {

/// Makes synced folders visible in file managers (Dolphin "Places").
class PlacesIntegration {
public:
    virtual ~PlacesIntegration() = default;
    virtual void addPlace(const QString& localPath, const QString& label) = 0;
    virtual void removePlace(const QString& localPath) = 0;
};

/// Desktop notifications (used by unattended runs).
class Notifier {
public:
    virtual ~Notifier() = default;
    virtual void notify(const QString& eventId, const QString& title, const QString& text) = 0;
};

} // namespace gds
