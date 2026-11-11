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

    /// Asynchronously opens the store; \c opened() is emitted when done.
    virtual void open() = 0;
    virtual bool isOpen() const = 0;
    virtual QString read(const QString& key) const = 0;
    virtual bool write(const QString& key, const QString& value) = 0;
    virtual bool remove(const QString& key) = 0;

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
        m_open = !failOpen;
        QMetaObject::invokeMethod(
            this, [this]() { Q_EMIT opened(m_open); }, Qt::QueuedConnection);
    }
    bool isOpen() const override { return m_open; }
    QString read(const QString& key) const override { return m_data.value(key); }
    bool write(const QString& key, const QString& value) override
    {
        if (!m_open)
            return false;
        m_data.insert(key, value);
        return true;
    }
    bool remove(const QString& key) override
    {
        m_data.remove(key);
        return true;
    }
    const QHash<QString, QString>& data() const { return m_data; }

    bool failOpen = false;

private:
    bool m_open = false;
    QHash<QString, QString> m_data;
};

namespace SecretKeys {
    inline QString clientId() { return QStringLiteral("oauth_client_id"); }
    inline QString clientSecret() { return QStringLiteral("oauth_client_secret"); }
    inline QString refreshToken() { return QStringLiteral("oauth_refresh_token"); }
}

} // namespace gds
