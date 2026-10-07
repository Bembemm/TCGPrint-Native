#pragma once

#include "import/UrlTransportPolicy.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <functional>

namespace tcgprint::imports {

struct UrlHttpResponse {
    int status{0};
    QMap<QString, QString> headers;
    QByteArray body;
};

struct UrlPayload {
    QByteArray bytes;
    QString mediaType;
    QUrl finalUrl;
    QMap<QString, QString> headers;
};

struct UrlFetchResponse {
    int status{0};
    QByteArray bytes;
    QString mediaType;
    QUrl finalUrl;
    QMap<QString, QString> headers;
};

using UrlRequestHeaders = QMap<QString, QString>;

using UrlHostResolver =
    std::function<QStringList(const QString& hostname)>;

using UrlPinnedRequestExecutor =
    std::function<UrlHttpResponse(
        const QUrl& logicalUrl,
        const QString& pinnedAddress,
        std::uint64_t timeoutMs,
        std::uint64_t maxResponseBytes
    )>;

using UrlPinnedRequestExecutorWithHeaders =
    std::function<UrlHttpResponse(
        const QUrl& logicalUrl,
        const QString& pinnedAddress,
        const UrlRequestHeaders& requestHeaders,
        std::uint64_t timeoutMs,
        std::uint64_t maxResponseBytes
    )>;

struct UrlFetchOptions {
    UrlTransportPolicy policy;
    UrlHostResolver resolveHost;
    UrlPinnedRequestExecutor requestExecutor;
    UrlRequestHeaders requestHeaders;
    UrlPinnedRequestExecutorWithHeaders requestExecutorWithHeaders;
};

UrlFetchResponse fetchUrlResponse(
    const QString& value,
    const UrlFetchOptions& options = {}
);

UrlFetchResponse fetchUrlResponse(
    const QUrl& value,
    const UrlFetchOptions& options = {}
);

UrlPayload fetchUrlPayload(
    const QString& value,
    const UrlFetchOptions& options = {}
);

UrlPayload fetchUrlPayload(
    const QUrl& value,
    const UrlFetchOptions& options = {}
);

namespace detail {

UrlHttpResponse executePinnedHttpGet(
    const QUrl& logicalUrl,
    const QString& pinnedAddress,
    std::uint64_t timeoutMs,
    std::uint64_t maxResponseBytes,
    const UrlRequestHeaders& requestHeaders = {}
);

} // namespace detail

} // namespace tcgprint::imports
