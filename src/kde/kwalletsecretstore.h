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
/// The wallet service can close a wallet at any time after it was opened
/// (idle timeout, screen locker, the user locking it in KWalletManager, a
/// restart of kwalletd). The values read at start are therefore kept in
/// memory so that the application keeps working, and the wallet is asked to
/// open again as soon as that happens.
class KWalletSecretStore : public SecretStore {
    Q_OBJECT
public:
    explicit KWalletSecretStore(QObject* parent = nullptr);
    ~KWalletSecretStore() override;

    void setWindowId(WId id) { m_windowId = id; }

    void open() override;
    bool isOpen() const override;
    QString read(const QString& key) const override;
    bool write(const QString& key, const QString& value) override;
    bool remove(const QString& key) override;
    QString lastError() const override { return m_lastError; }

private:
    void requestOpen();
    void onOpened(bool ok);
    void onClosed();

    std::unique_ptr<KWallet::Wallet> m_wallet;
    WId m_windowId = 0;
    bool m_everOpened = false;
    bool m_opening = false;
    QHash<QString, QString> m_cache; ///< values of our keys, valid while m_everOpened
    QSet<QString> m_pendingRemovals; ///< removals to repeat once the wallet is open again
    QString m_lastError;
};

} // namespace gds
