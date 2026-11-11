#include "http.h"

#include <QEventLoop>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace gds {

HttpResponse httpSend(QNetworkAccessManager& nam, const HttpRequest& request, const std::atomic_bool* cancel,
    const HttpProgress& progress)
{
    HttpResponse response;
    if (cancel && cancel->load()) {
        response.cancelled = true;
        return response;
    }

    QNetworkRequest req(request.url);
    for (const auto& h : request.headers)
        req.setRawHeader(h.first, h.second);
    req.setTransferTimeout(request.transferTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        request.followRedirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                : QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = nullptr;
    const bool upload = request.bodyDevice != nullptr;
    if (upload) {
        if (request.bodyDeviceSize >= 0)
            req.setHeader(QNetworkRequest::ContentLengthHeader, request.bodyDeviceSize);
        reply = nam.sendCustomRequest(req, request.method, request.bodyDevice);
    } else {
        reply = nam.sendCustomRequest(req, request.method, request.body);
    }

    QEventLoop loop;
    bool sinkFailed = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (request.sink && status >= 200 && status < 300) {
            const QByteArray chunk = reply->readAll();
            if (request.sink->write(chunk) != chunk.size()) {
                sinkFailed = true;
                reply->abort();
            }
        }
    });
    if (progress) {
        if (upload) {
            QObject::connect(reply, &QNetworkReply::uploadProgress, &loop,
                [&](qint64 done, qint64 total) { progress(done, total); });
        } else if (request.sink) {
            QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                [&](qint64 done, qint64 total) { progress(done, total); });
        }
    }

    QTimer cancelPoll;
    if (cancel) {
        QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&]() {
            if (cancel->load()) {
                response.cancelled = true;
                reply->abort();
            }
        });
        cancelPoll.start(50);
    }

    loop.exec();
    cancelPoll.stop();

    response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto rawHeaders = reply->rawHeaderPairs();
    for (const auto& h : rawHeaders)
        response.headers.insert(h.first.toLower(), h.second);
    const QByteArray rest = reply->isOpen() ? reply->readAll() : QByteArray();
    if (request.sink && response.isSuccess()) {
        if (!rest.isEmpty() && request.sink->write(rest) != rest.size())
            sinkFailed = true;
    } else {
        response.body = rest;
    }
    response.networkError = reply->error();
    response.errorString = reply->errorString();
    if (sinkFailed) {
        response.status = 0;
        response.networkError = QNetworkReply::UnknownContentError;
        response.errorString = QStringLiteral("Could not write to the destination file");
    }
    reply->deleteLater();
    return response;
}

QByteArray formEncode(const QList<QPair<QString, QString>>& fields)
{
    QUrlQuery q;
    for (const auto& f : fields) {
        q.addQueryItem(QString::fromUtf8(QUrl::toPercentEncoding(f.first)),
            QString::fromUtf8(QUrl::toPercentEncoding(f.second)));
    }
    return q.query(QUrl::FullyEncoded).toUtf8();
}

} // namespace gds
