#pragma once

#include "secretstore.h"

#include <QHash>
#include <QSet>
#include <QWindow>

#include <memory>

namespace KWallet {
class Wallet;
}

namespace gds {

/// Secrets in the KDE wallet (folder "gdrive-sync" of the network wallet).
///
/// The application opens the wallet only for an action that needs it and
/// closes it again afterwards. The wallet service can also close the wallet
/// at any time (idle timeout, screen locker, the user locking it); the store
/// then simply stays closed until the next action opens it. It never asks to
/// open the wallet by itself, because every such request can show the
/// password prompt.
class KWalletSecretStore : public SecretStore {
    Q_OBJECT
public:
    explicit KWalletSecretStore(QObject* parent = nullptr);
    ~KWalletSecretStore() override;

    void setWindowId(WId id) { m_windowId = id; }

    void open() override;
    void close() override;
    bool isOpen() const override;
    QString read(const QString& key) const override;
    bool write(const QString& key, const QString& value) override;
    bool remove(const QString& key) override;
    QString lastError() const override { return m_lastError; }

private:
    void onOpened(bool ok);
    void onClosed();
    void dropWallet();

    std::unique_ptr<KWallet::Wallet> m_wallet;
    WId m_windowId = 0;
    bool m_opening = false;
    QHash<QString, QString> m_cache; ///< values of our keys while the wallet is open
    QSet<QString> m_pendingRemovals; ///< removals to repeat the next time the wallet is open
    QString m_lastError;
};

} // namespace gds
