#include "pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace gds {

QString Pkce::generateVerifier(int length)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    length = qBound(43, length, 128);
    QString out;
    out.reserve(length);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < length; ++i)
        out.append(QLatin1Char(alphabet[rng->bounded(int(sizeof(alphabet) - 1))]));
    return out;
}

QString Pkce::challengeS256(const QString& verifier)
{
    const QByteArray hash = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString Pkce::randomToken(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < bytes; ++i)
        raw[i] = char(rng->bounded(256));
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace gds
