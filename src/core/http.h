#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QNetworkReply>
#include <QPair>
#include <QUrl>

#include <atomic>
#include <functional>

class QIODevice;
class QNetworkAccessManager;

namespace gds {

using HttpHeader = QPair<QByteArray, QByteArray>;

struct HttpRequest {
    QByteArray method = "GET";
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    /// When set, the request body is streamed from this device instead of \c body.
    QIODevice* bodyDevice = nullptr;
    qint64 bodyDeviceSize = -1;
    /// When set, a successful (2xx) response body is streamed into this device.
    QIODevice* sink = nullptr;
    bool followRedirects = false;
    /// Abort when no bytes moved for this long.
    int transferTimeoutMs = 60000;
};

struct HttpResponse {
    int status = 0; ///< HTTP status, 0 when no response was received
    QByteArray body;
    QHash<QByteArray, QByteArray> headers; ///< keys are lower-cased
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    QString errorString;
    bool cancelled = false;

    bool isSuccess() const { return status >= 200 && status < 300; }
    QByteArray header(const QByteArray& name) const { return headers.value(name.toLower()); }
};

using HttpProgress = std::function<void(qint64 done, qint64 total)>;

/// Sends a request and blocks the calling thread until it finished while
/// running a local event loop. Must only be called from worker threads, never
/// from the GUI thread. \p nam must live in the calling thread.
HttpResponse httpSend(QNetworkAccessManager& nam, const HttpRequest& request,
    const std::atomic_bool* cancel = nullptr, const HttpProgress& progress = {});

QByteArray formEncode(const QList<QPair<QString, QString>>& fields);

} // namespace gds
