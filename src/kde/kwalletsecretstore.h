#pragma once

#include "secretstore.h"

#include <QWindow>

#include <memory>

namespace KWallet {
class Wallet;
}

namespace gds {

/// Secrets in the KDE wallet (folder "gdrive-sync" of the network wallet).
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

private:
    void onOpened(bool ok);

    std::unique_ptr<KWallet::Wallet> m_wallet;
    WId m_windowId = 0;
};

} // namespace gds
