#include "kwalletsecretstore.h"

#include <KWallet>

namespace gds {

namespace {
    QString folderName()
    {
        return QStringLiteral("gdrive-sync");
    }
}

KWalletSecretStore::KWalletSecretStore(QObject* parent)
    : SecretStore(parent)
{
}

KWalletSecretStore::~KWalletSecretStore() = default;

void KWalletSecretStore::open()
{
    if (isOpen()) {
        QMetaObject::invokeMethod(this, [this]() { Q_EMIT opened(true); }, Qt::QueuedConnection);
        return;
    }
    // Asynchronous: the password prompt of the wallet never blocks our event loop.
    m_wallet.reset(KWallet::Wallet::openWallet(
        KWallet::Wallet::NetworkWallet(), m_windowId, KWallet::Wallet::Asynchronous));
    if (!m_wallet) {
        QMetaObject::invokeMethod(this, [this]() { Q_EMIT opened(false); }, Qt::QueuedConnection);
        return;
    }
    connect(m_wallet.get(), &KWallet::Wallet::walletOpened, this, &KWalletSecretStore::onOpened);
}

void KWalletSecretStore::onOpened(bool ok)
{
    if (ok) {
        if (!m_wallet->hasFolder(folderName()))
            ok = m_wallet->createFolder(folderName());
        ok = ok && m_wallet->setFolder(folderName());
    }
    if (!ok)
        m_wallet.reset();
    Q_EMIT opened(ok);
}

bool KWalletSecretStore::isOpen() const
{
    return m_wallet && m_wallet->isOpen();
}

QString KWalletSecretStore::read(const QString& key) const
{
    QString value;
    if (isOpen() && m_wallet->hasEntry(key))
        m_wallet->readPassword(key, value);
    return value;
}

bool KWalletSecretStore::write(const QString& key, const QString& value)
{
    return isOpen() && m_wallet->writePassword(key, value) == 0;
}

bool KWalletSecretStore::remove(const QString& key)
{
    if (!isOpen())
        return false;
    return !m_wallet->hasEntry(key) || m_wallet->removeEntry(key) == 0;
}

} // namespace gds
