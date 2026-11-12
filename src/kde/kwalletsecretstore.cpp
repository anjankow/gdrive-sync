#include "kwalletsecretstore.h"

#include "logger.h"

#include <KWallet>

namespace gds {

namespace {
    QString folderName()
    {
        return QStringLiteral("gdrive-sync");
    }

    QStringList knownKeys()
    {
        return {SecretKeys::clientId(), SecretKeys::clientSecret(), SecretKeys::refreshToken()};
    }

    QString closedReason()
    {
        return QStringLiteral("the KDE wallet was closed or locked while the application was using it; "
                              "try again and unlock it when the wallet asks for the password");
    }
}

KWalletSecretStore::KWalletSecretStore(QObject* parent)
    : SecretStore(parent)
{
}

KWalletSecretStore::~KWalletSecretStore()
{
    if (!m_pendingRemovals.isEmpty()) {
        logError(
            QStringLiteral("%1 entry(ies) could not be removed from the KDE wallet because it stayed closed")
                .arg(m_pendingRemovals.size()));
    }
}

void KWalletSecretStore::open()
{
    if (isOpen()) {
        QMetaObject::invokeMethod(this, [this]() { Q_EMIT opened(true); }, Qt::QueuedConnection);
        return;
    }
    if (m_opening)
        return; // opened() follows
    dropWallet();
    // Asynchronous: the password prompt of the wallet never blocks our event loop.
    m_wallet.reset(KWallet::Wallet::openWallet(
        KWallet::Wallet::NetworkWallet(), m_windowId, KWallet::Wallet::Asynchronous));
    if (!m_wallet) {
        QMetaObject::invokeMethod(this, [this]() { onOpened(false); }, Qt::QueuedConnection);
        return;
    }
    m_opening = true;
    connect(m_wallet.get(), &KWallet::Wallet::walletOpened, this, &KWalletSecretStore::onOpened);
    connect(m_wallet.get(), &KWallet::Wallet::walletClosed, this, &KWalletSecretStore::onClosed);
}

void KWalletSecretStore::close()
{
    m_opening = false;
    dropWallet();
}

void KWalletSecretStore::dropWallet()
{
    m_cache.clear();
    if (m_wallet) {
        m_wallet->disconnect(this);
        m_wallet.release()->deleteLater(); // we may be inside one of its signals
    }
}

void KWalletSecretStore::onOpened(bool ok)
{
    m_opening = false;
    if (ok) {
        if (!m_wallet->hasFolder(folderName()))
            ok = m_wallet->createFolder(folderName());
        ok = ok && m_wallet->setFolder(folderName());
    }
    if (!ok) {
        dropWallet();
        Q_EMIT opened(false);
        return;
    }
    const QStringList keys = knownKeys();
    for (const QString& key : keys) {
        QString value;
        if (m_wallet->hasEntry(key))
            m_wallet->readPassword(key, value);
        m_cache.insert(key, value);
    }
    const QSet<QString> pending = m_pendingRemovals;
    m_pendingRemovals.clear();
    for (const QString& key : pending)
        remove(key);
    Q_EMIT opened(true);
}

void KWalletSecretStore::onClosed()
{
    // Nothing is reopened here: the next action that needs the wallet opens it.
    logDebug(QStringLiteral("The KDE wallet was closed by the wallet service"));
    QMetaObject::invokeMethod(this, [this]() { dropWallet(); }, Qt::QueuedConnection);
}

bool KWalletSecretStore::isOpen() const
{
    return m_wallet && !m_opening && m_wallet->isOpen();
}

QString KWalletSecretStore::read(const QString& key) const
{
    return isOpen() ? m_cache.value(key) : QString();
}

bool KWalletSecretStore::write(const QString& key, const QString& value)
{
    if (!isOpen()) {
        m_lastError = m_wallet ? closedReason() : QStringLiteral("the KDE wallet is not open");
        return false;
    }
    const int rc = m_wallet->writePassword(key, value);
    if (rc != 0) {
        m_lastError = m_wallet->isOpen()
            ? QStringLiteral(
                  "the KDE wallet service rejected the entry (error %1); kwalletd may have been restarted")
                  .arg(rc)
            : closedReason();
        return false;
    }
    m_lastError.clear();
    m_cache.insert(key, value);
    m_pendingRemovals.remove(key);
    return true;
}

bool KWalletSecretStore::remove(const QString& key)
{
    m_cache.insert(key, QString());
    if (!isOpen()) {
        m_lastError = m_wallet ? closedReason() : QStringLiteral("the KDE wallet is not open");
        m_pendingRemovals.insert(key); // repeated the next time the wallet is open
        return false;
    }
    if (m_wallet->hasEntry(key) && m_wallet->removeEntry(key) != 0) {
        m_lastError = QStringLiteral("the KDE wallet service did not remove the entry");
        m_pendingRemovals.insert(key);
        return false;
    }
    m_lastError.clear();
    m_pendingRemovals.remove(key);
    return true;
}

} // namespace gds
