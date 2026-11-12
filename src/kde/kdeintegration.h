#pragma once

#include "integration.h"

#include <KService>

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

/// Starts the desktop's programs for folders and web pages.
///
/// They are started from the home directory, with the desktop's own
/// environment (see hostEnvironment()) and with their output kept off the
/// terminal. KDE itself would start the file manager for a local folder in the
/// folder's parent directory: the file manager would then live on in a
/// directory that unmounting (or the user) may delete, and every program it
/// starts afterwards fails there (fontconfig: "out of memory").
class ExternalPrograms {
public:
    /// \p service: the file manager; by default the preferred one for folders.
    static bool openFolder(const QString& localPath, KService::Ptr service = {});
    /// Opens a web page in the default browser (xdg-open).
    static bool openUrl(const QUrl& url);
    /// Starts \p program detached as described above.
    static bool start(const QString& program, const QStringList& arguments);
};

class KdeNotifier : public Notifier {
public:
    void notify(const QString& eventId, const QString& title, const QString& text) override;
};

} // namespace gds
