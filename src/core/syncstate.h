#pragma once

#include "synctypes.h"

namespace gds {

/// Persistent per-folder record of the last synced state (JSON file).
class SyncState {
public:
    explicit SyncState(QString filePath);

    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr) const;

    StateMap& entries() { return m_entries; }
    const StateMap& entries() const { return m_entries; }
    QString filePath() const { return m_filePath; }

    static bool removeFile(const QString& filePath);

private:
    QString m_filePath;
    StateMap m_entries;
};

} // namespace gds
