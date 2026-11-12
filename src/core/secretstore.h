#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace gds {

/// Storage for authentication secrets. The production implementation is
/// backed by KWallet; nothing secret is ever written to plain config files.
class SecretStore : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    /// Asynchronously opens the store; \c opened() is emitted when done (also
    /// when it was already open).
    virtual void open() = 0;
    /// Stops using the store and forgets the values read from it.
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    /// Valid only while the store is open.
    virtual QString read(const QString& key) const = 0;
    virtual bool write(const QString& key, const QString& value) = 0;
    virtual bool remove(const QString& key) = 0;
    /// Why the last write() or remove() failed; empty after a success.
    virtual QString lastError() const = 0;

Q_SIGNALS:
    void opened(bool ok);
};

/// Volatile store used by tests.
class MemorySecretStore : public SecretStore {
    Q_OBJECT
public:
    using SecretStore::SecretStore;

    void open() override
    {
        if (!m_open)
            ++openCount;
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_open = !failOpen;
                Q_EMIT opened(m_open);
            },
            Qt::QueuedConnection);
    }
    void close() override
    {
        if (m_open)
            ++closeCount;
        m_open = false;
    }
    bool isOpen() const override { return m_open; }
    QString read(const QString& key) const override { return m_open ? m_data.value(key) : QString(); }
    bool write(const QString& key, const QString& value) override
    {
        if (!m_open || failWrites || key == failWriteOfKey) {
            m_lastError = failReason;
            return false;
        }
        m_lastError.clear();
        m_data.insert(key, value);
        return true;
    }
    bool remove(const QString& key) override
    {
        if (!m_open) {
            m_lastError = failReason;
            return false;
        }
        m_data.remove(key);
        return true;
    }
    /// Test check: the stored value, also while the store is closed.
    QString stored(const QString& key) const { return m_data.value(key); }
    /// Test setup: stores a value without opening the store.
    void preset(const QString& key, const QString& value) { m_data.insert(key, value); }
    QString lastError() const override { return m_lastError; }
    const QHash<QString, QString>& data() const { return m_data; }

    int openCount = 0; ///< open() calls, i.e. password prompts of a closed wallet
    int closeCount = 0;
    bool failOpen = false;
    bool failWrites = false; ///< simulate a wallet that was closed after opening
    QString failWriteOfKey; ///< simulate a failure in the middle of a change
    QString failReason = QStringLiteral("the wallet is closed");

private:
    bool m_open = false;
    QHash<QString, QString> m_data;
    QString m_lastError;
};

namespace SecretKeys {
    inline QString clientId()
    {
        return QStringLiteral("oauth_client_id");
    }
    inline QString clientSecret()
    {
        return QStringLiteral("oauth_client_secret");
    }
    inline QString refreshToken()
    {
        return QStringLiteral("oauth_refresh_token");
    }
}

} // namespace gds
