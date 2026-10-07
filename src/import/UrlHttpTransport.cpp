#include "import/UrlHttpTransport.h"

#include "import/ImportFailure.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

namespace tcgprint::imports {
namespace {

constexpr qsizetype MaxHeaderBytes = 64 * 1024;

bool isRedirectStatus(int status)
{
    return status == 301
        || status == 302
        || status == 303
        || status == 307
        || status == 308;
}

int remainingSocketMs(
    const QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    const qint64 elapsed = timer.elapsed();
    if (
        elapsed < 0
        || static_cast<std::uint64_t>(elapsed) >= timeoutMs
    ) {
        throw ImportFailureError(
            QStringLiteral("Remote request exceeded its timeout."),
            QStringLiteral("URL_TIMEOUT")
        );
    }

    const std::uint64_t remaining =
        timeoutMs - static_cast<std::uint64_t>(elapsed);
    return static_cast<int>(
        std::min<std::uint64_t>(
            remaining,
            static_cast<std::uint64_t>(
                std::numeric_limits<int>::max()
            )
        )
    );
}

QString requestHostHeader(const QUrl& url)
{
    QString host;
    const QByteArray ace = QUrl::toAce(url.host());
    if (!ace.isEmpty()) {
        host = QString::fromLatin1(ace);
    } else {
        host = url.host();
    }

    if (host.contains(QLatin1Char(':'))) {
        host = QStringLiteral("[") + host + QStringLiteral("]");
    }

    const int port = url.port(-1);
    const bool defaultPort =
        port < 0
        || (
            url.scheme() == QStringLiteral("https")
            && port == 443
        )
        || (
            url.scheme() == QStringLiteral("http")
            && port == 80
        );
    if (!defaultPort) {
        host += QStringLiteral(":") + QString::number(port);
    }
    return host;
}

QByteArray requestTarget(const QUrl& url)
{
    QString target = url.path(QUrl::FullyEncoded);
    if (target.isEmpty()) {
        target = QStringLiteral("/");
    }

    const QString query = url.query(QUrl::FullyEncoded);
    if (!query.isEmpty()) {
        target += QStringLiteral("?") + query;
    }
    return target.toUtf8();
}

void writeAll(
    QAbstractSocket& socket,
    const QByteArray& bytes,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    qint64 offset = 0;
    while (offset < bytes.size()) {
        const qint64 written =
            socket.write(
                bytes.constData() + offset,
                bytes.size() - offset
            );
        if (written < 0) {
            throw ImportFailureError(
                QStringLiteral(
                    "The remote request could not be written."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
        offset += written;

        if (
            socket.bytesToWrite() > 0
            && !socket.waitForBytesWritten(
                remainingSocketMs(timer, timeoutMs)
            )
        ) {
            remainingSocketMs(timer, timeoutMs);
            throw ImportFailureError(
                QStringLiteral(
                    "The remote request could not be written."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
    }
}

void appendAvailable(
    QAbstractSocket& socket,
    QByteArray& buffer
)
{
    const QByteArray data = socket.readAll();
    if (!data.isEmpty()) {
        buffer += data;
    }
}

void waitForInput(
    QAbstractSocket& socket,
    QByteArray& buffer,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    appendAvailable(socket, buffer);
    if (!buffer.isEmpty()) {
        return;
    }

    if (
        socket.state() == QAbstractSocket::UnconnectedState
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "The remote server closed the connection unexpectedly."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    if (!socket.waitForReadyRead(
            remainingSocketMs(timer, timeoutMs)
        )) {
        appendAvailable(socket, buffer);
        if (!buffer.isEmpty()) {
            return;
        }
        remainingSocketMs(timer, timeoutMs);
        throw ImportFailureError(
            QStringLiteral(
                "The remote response could not be read."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }
    appendAvailable(socket, buffer);
}

void ensureBufferSize(
    QAbstractSocket& socket,
    QByteArray& buffer,
    qsizetype required,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    while (buffer.size() < required) {
        QByteArray incoming;
        waitForInput(
            socket,
            incoming,
            timer,
            timeoutMs
        );
        buffer += incoming;
    }
}

QMap<QString, QString> parseHeaders(
    const QByteArray& headerBlock,
    int& status
)
{
    const QList<QByteArray> lines =
        headerBlock.split('\n');
    if (lines.isEmpty()) {
        throw ImportFailureError(
            QStringLiteral("Remote HTTP response has no status line."),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    QByteArray statusLine = lines.front().trimmed();
    const QRegularExpression statusPattern(
        QStringLiteral(R"(^HTTP/1\.[01]\s+([0-9]{3})(?:\s|$))")
    );
    const auto match = statusPattern.match(
        QString::fromLatin1(statusLine)
    );
    if (!match.hasMatch()) {
        throw ImportFailureError(
            QStringLiteral("Remote HTTP status line is invalid."),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    bool ok = false;
    status = match.captured(1).toInt(&ok);
    if (!ok) {
        throw ImportFailureError(
            QStringLiteral("Remote HTTP status is invalid."),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    QMap<QString, QString> headers;
    for (int index = 1; index < lines.size(); ++index) {
        QByteArray line = lines[index];
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        if (
            line.startsWith(' ')
            || line.startsWith('\t')
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote HTTP response contains folded headers."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote HTTP response contains an invalid header."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        const QString key =
            QString::fromLatin1(
                line.left(colon).trimmed()
            ).toLower();
        const QString value =
            QString::fromLatin1(
                line.mid(colon + 1).trimmed()
            );

        if (headers.contains(key)) {
            headers[key] +=
                QStringLiteral(", ") + value;
        } else {
            headers.insert(key, value);
        }
    }
    return headers;
}

std::optional<std::uint64_t> contentLength(
    const QMap<QString, QString>& headers
)
{
    const auto found =
        headers.constFind(QStringLiteral("content-length"));
    if (found == headers.cend()) {
        return std::nullopt;
    }

    const QString value = found.value().trimmed();
    if (
        !QRegularExpression(
            QStringLiteral(R"(^[0-9]+$)")
        ).match(value).hasMatch()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote Content-Length is invalid."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok);
    if (!ok) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote Content-Length is invalid."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }
    return static_cast<std::uint64_t>(parsed);
}

QByteArray readFixedBody(
    QAbstractSocket& socket,
    QByteArray buffer,
    std::uint64_t length,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs,
    std::uint64_t maximum
)
{
    validateUrlResponseSize(
        length,
        static_cast<std::uint64_t>(buffer.size()),
        maximum
    );

    if (
        length
        > static_cast<std::uint64_t>(
            std::numeric_limits<qsizetype>::max()
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote response is too large for this platform."
            ),
            QStringLiteral("URL_RESPONSE_LIMIT")
        );
    }

    const qsizetype target =
        static_cast<qsizetype>(length);
    while (buffer.size() < target) {
        QByteArray incoming;
        waitForInput(
            socket,
            incoming,
            timer,
            timeoutMs
        );
        buffer += incoming;
        validateUrlResponseSize(
            length,
            static_cast<std::uint64_t>(buffer.size()),
            maximum
        );
    }
    buffer.truncate(target);
    return buffer;
}

QByteArray readUntilClose(
    QAbstractSocket& socket,
    QByteArray buffer,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs,
    std::uint64_t maximum
)
{
    validateUrlResponseSize(
        std::nullopt,
        static_cast<std::uint64_t>(buffer.size()),
        maximum
    );

    while (
        socket.state()
        != QAbstractSocket::UnconnectedState
    ) {
        if (socket.bytesAvailable() == 0) {
            if (!socket.waitForReadyRead(
                    remainingSocketMs(timer, timeoutMs)
                )) {
                appendAvailable(socket, buffer);
                if (
                    socket.state()
                    == QAbstractSocket::UnconnectedState
                ) {
                    break;
                }
                remainingSocketMs(timer, timeoutMs);
                throw ImportFailureError(
                    QStringLiteral(
                        "Remote response did not finish before timeout."
                    ),
                    QStringLiteral("URL_TIMEOUT")
                );
            }
        }
        appendAvailable(socket, buffer);
        validateUrlResponseSize(
            std::nullopt,
            static_cast<std::uint64_t>(buffer.size()),
            maximum
        );
    }

    appendAvailable(socket, buffer);
    validateUrlResponseSize(
        std::nullopt,
        static_cast<std::uint64_t>(buffer.size()),
        maximum
    );
    return buffer;
}

void ensureChunkData(
    QAbstractSocket& socket,
    QByteArray& buffer,
    qsizetype required,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    ensureBufferSize(
        socket,
        buffer,
        required,
        timer,
        timeoutMs
    );
}

QByteArray readChunkedBody(
    QAbstractSocket& socket,
    QByteArray buffer,
    QElapsedTimer& timer,
    std::uint64_t timeoutMs,
    std::uint64_t maximum
)
{
    QByteArray decoded;

    while (true) {
        qsizetype lineEnd = buffer.indexOf("\r\n");
        while (lineEnd < 0) {
            if (buffer.size() > 4096) {
                throw ImportFailureError(
                    QStringLiteral(
                        "Remote chunk header is too large."
                    ),
                    QStringLiteral("URL_HTTP_ERROR")
                );
            }
            QByteArray incoming;
            waitForInput(
                socket,
                incoming,
                timer,
                timeoutMs
            );
            buffer += incoming;
            lineEnd = buffer.indexOf("\r\n");
        }

        const QByteArray rawSizeLine =
            buffer.left(lineEnd);
        buffer.remove(0, lineEnd + 2);

        const QByteArray sizeToken =
            rawSizeLine.split(';').front().trimmed();
        bool ok = false;
        const qulonglong chunkSize =
            sizeToken.toULongLong(&ok, 16);
        if (!ok) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote chunk size is invalid."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        if (chunkSize == 0) {
            if (buffer.startsWith("\r\n")) {
                return decoded;
            }

            qsizetype trailerEnd =
                buffer.indexOf("\r\n\r\n");
            while (trailerEnd < 0) {
                if (buffer.size() > MaxHeaderBytes) {
                    throw ImportFailureError(
                        QStringLiteral(
                            "Remote chunk trailers are too large."
                        ),
                        QStringLiteral("URL_HTTP_ERROR")
                    );
                }
                QByteArray incoming;
                waitForInput(
                    socket,
                    incoming,
                    timer,
                    timeoutMs
                );
                buffer += incoming;
                trailerEnd =
                    buffer.indexOf("\r\n\r\n");
            }
            return decoded;
        }

        if (
            chunkSize
            > maximum
            - std::min<std::uint64_t>(
                maximum,
                static_cast<std::uint64_t>(
                    decoded.size()
                )
            )
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote response exceeds the configured byte limit."
                ),
                QStringLiteral("URL_RESPONSE_LIMIT")
            );
        }

        if (
            chunkSize
            > static_cast<qulonglong>(
                std::numeric_limits<qsizetype>::max() - 2
            )
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote chunk is too large for this platform."
                ),
                QStringLiteral("URL_RESPONSE_LIMIT")
            );
        }

        const qsizetype required =
            static_cast<qsizetype>(chunkSize) + 2;
        ensureChunkData(
            socket,
            buffer,
            required,
            timer,
            timeoutMs
        );

        if (
            buffer.mid(
                static_cast<qsizetype>(chunkSize),
                2
            ) != QByteArrayLiteral("\r\n")
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote chunk framing is invalid."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        decoded += buffer.left(
            static_cast<qsizetype>(chunkSize)
        );
        buffer.remove(0, required);
        validateUrlResponseSize(
            std::nullopt,
            static_cast<std::uint64_t>(decoded.size()),
            maximum
        );
    }
}

QStringList defaultResolveHost(const QString& hostname)
{
    const QHostInfo info = QHostInfo::fromName(hostname);
    if (info.error() != QHostInfo::NoError) {
        throw ImportFailureError(
            QStringLiteral(
                "The remote host could not be resolved."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    QStringList addresses;
    for (const QHostAddress& address : info.addresses()) {
        addresses.push_back(address.toString());
    }
    return addresses;
}

void validateInitialFetchUrl(const QUrl& url)
{
    if (
        url.scheme() != QStringLiteral("http")
        && url.scheme() != QStringLiteral("https")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Only HTTP and HTTPS URLs can be fetched."
            ),
            QStringLiteral("URL_UNSUPPORTED")
        );
    }
    if (
        !url.isValid()
        || url.host().isEmpty()
        || !url.userName().isEmpty()
        || !url.password().isEmpty()
    ) {
        throw ImportFailureError(
            QStringLiteral("The supplied URL is invalid."),
            QStringLiteral("URL_INVALID")
        );
    }
}

QStringList resolveAddresses(
    const QUrl& url,
    const UrlFetchOptions& options
)
{
    QHostAddress direct;
    if (direct.setAddress(url.host())) {
        return QStringList{url.host()};
    }

    try {
        return options.resolveHost
            ? options.resolveHost(url.host())
            : defaultResolveHost(url.host());
    } catch (const ImportFailureError&) {
        throw;
    } catch (const std::exception&) {
        throw ImportFailureError(
            QStringLiteral(
                "The remote host could not be resolved."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }
}

std::uint64_t remainingFetchMs(
    const QElapsedTimer& timer,
    std::uint64_t timeoutMs
)
{
    const qint64 elapsed = timer.elapsed();
    if (
        elapsed < 0
        || static_cast<std::uint64_t>(elapsed) >= timeoutMs
    ) {
        throw ImportFailureError(
            QStringLiteral("Remote request exceeded its timeout."),
            QStringLiteral("URL_TIMEOUT")
        );
    }
    return timeoutMs - static_cast<std::uint64_t>(elapsed);
}

QString responseMediaType(
    const QMap<QString, QString>& headers
)
{
    const QString raw =
        headers.value(
            QStringLiteral("content-type"),
            QStringLiteral("application/octet-stream")
        );
    return raw.section(QLatin1Char(';'), 0, 0)
        .trimmed()
        .toLower();
}

} // namespace

namespace detail {

UrlHttpResponse executePinnedHttpGet(
    const QUrl& logicalUrl,
    const QString& pinnedAddress,
    std::uint64_t timeoutMs,
    std::uint64_t maxResponseBytes
)
{
    QElapsedTimer timer;
    timer.start();

    const int defaultPort =
        logicalUrl.scheme() == QStringLiteral("https")
        ? 443
        : 80;
    const int requestedPort =
        logicalUrl.port(defaultPort);
    if (requestedPort < 1 || requestedPort > 65535) {
        throw ImportFailureError(
            QStringLiteral("Remote URL has an invalid port."),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }
    const quint16 port =
        static_cast<quint16>(requestedPort);

    QHostAddress pinnedHost(pinnedAddress);
    const auto protocol =
        pinnedHost.protocol();

    std::unique_ptr<QAbstractSocket> socket;
    if (logicalUrl.scheme() == QStringLiteral("https")) {
        auto ssl = std::make_unique<QSslSocket>();
        ssl->connectToHostEncrypted(
            pinnedAddress,
            port,
            logicalUrl.host(),
            QIODeviceBase::ReadWrite,
            protocol
        );
        if (!ssl->waitForEncrypted(
                remainingSocketMs(timer, timeoutMs)
            )) {
            remainingSocketMs(timer, timeoutMs);
            throw ImportFailureError(
                QStringLiteral(
                    "The HTTPS connection could not be established securely."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
        socket = std::move(ssl);
    } else {
        auto tcp = std::make_unique<QTcpSocket>();
        tcp->connectToHost(
            pinnedAddress,
            port,
            QIODeviceBase::ReadWrite,
            protocol
        );
        if (!tcp->waitForConnected(
                remainingSocketMs(timer, timeoutMs)
            )) {
            remainingSocketMs(timer, timeoutMs);
            throw ImportFailureError(
                QStringLiteral(
                    "The HTTP connection could not be established."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
        socket = std::move(tcp);
    }

    QByteArray request;
    request += QByteArrayLiteral("GET ");
    request += requestTarget(logicalUrl);
    request += QByteArrayLiteral(" HTTP/1.1\r\nHost: ");
    request += requestHostHeader(logicalUrl).toLatin1();
    request += QByteArrayLiteral(
        "\r\nAccept: text/html, application/xhtml+xml, image/*, text/plain, text/csv, text/tab-separated-values, application/json, application/xml, text/xml, application/zip, application/octet-stream;q=0.9"
        "\r\nUser-Agent: TCGPrint/0.1.0 (+https://github.com/Bembemm/TCGPrint)"
        "\r\nAccept-Encoding: identity"
        "\r\nConnection: close\r\n\r\n"
    );

    writeAll(
        *socket,
        request,
        timer,
        timeoutMs
    );

    QByteArray received;
    qsizetype headerEnd = -1;
    while (headerEnd < 0) {
        QByteArray incoming;
        waitForInput(
            *socket,
            incoming,
            timer,
            timeoutMs
        );
        received += incoming;
        if (received.size() > MaxHeaderBytes) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote HTTP response headers are too large."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
        headerEnd = received.indexOf("\r\n\r\n");
    }

    const QByteArray headerBlock =
        received.left(headerEnd);
    QByteArray body =
        received.mid(headerEnd + 4);

    int status = 0;
    const QMap<QString, QString> headers =
        parseHeaders(headerBlock, status);

    if (status == 101) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote server attempted an unsupported protocol upgrade."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    if (
        isRedirectStatus(status)
        || status < 200
        || status >= 300
        || status == 204
        || status == 205
    ) {
        socket->abort();
        return UrlHttpResponse{
            .status = status,
            .headers = headers,
            .body = {},
        };
    }

    const QString transferEncoding =
        headers.value(
            QStringLiteral("transfer-encoding")
        ).toLower();
    const auto length = contentLength(headers);

    if (
        !transferEncoding.isEmpty()
        && length.has_value()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote response contains conflicting body framing."
            ),
            QStringLiteral("URL_HTTP_ERROR")
        );
    }

    if (!transferEncoding.isEmpty()) {
        const QStringList encodings =
            transferEncoding.split(
                QLatin1Char(','),
                Qt::SkipEmptyParts
            );
        if (
            encodings.isEmpty()
            || encodings.back().trimmed()
                != QStringLiteral("chunked")
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote response uses an unsupported transfer encoding."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }
        body = readChunkedBody(
            *socket,
            std::move(body),
            timer,
            timeoutMs,
            maxResponseBytes
        );
    } else if (length.has_value()) {
        body = readFixedBody(
            *socket,
            std::move(body),
            *length,
            timer,
            timeoutMs,
            maxResponseBytes
        );
    } else {
        body = readUntilClose(
            *socket,
            std::move(body),
            timer,
            timeoutMs,
            maxResponseBytes
        );
    }

    return UrlHttpResponse{
        .status = status,
        .headers = headers,
        .body = std::move(body),
    };
}

} // namespace detail

UrlPayload fetchUrlPayload(
    const QString& value,
    const UrlFetchOptions& options
)
{
    const QUrl url(value.trimmed(), QUrl::StrictMode);
    return fetchUrlPayload(url, options);
}

UrlPayload fetchUrlPayload(
    const QUrl& value,
    const UrlFetchOptions& options
)
{
    validateUrlTransportPolicy(options.policy);
    validateInitialFetchUrl(value);

    QElapsedTimer totalTimer;
    totalTimer.start();

    QUrl current = value;
    bool redirect = false;

    for (int redirects = 0; ; ++redirects) {
        const QStringList addresses =
            resolveAddresses(current, options);
        validateResolvedUrlHost(
            current,
            addresses,
            redirect
        );

        const QString pinnedAddress =
            QHostAddress(current.host()).isNull()
            ? addresses.front()
            : current.host();

        const std::uint64_t remaining =
            remainingFetchMs(
                totalTimer,
                options.policy.timeoutMs
            );

        UrlHttpResponse response;
        try {
            response = options.requestExecutor
                ? options.requestExecutor(
                    current,
                    pinnedAddress,
                    remaining,
                    options.policy.maxResponseBytes
                )
                : detail::executePinnedHttpGet(
                    current,
                    pinnedAddress,
                    remaining,
                    options.policy.maxResponseBytes
                );
        } catch (const ImportFailureError&) {
            throw;
        } catch (const std::exception&) {
            throw ImportFailureError(
                QStringLiteral(
                    "The remote server could not be reached."
                ),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        if (isRedirectStatus(response.status)) {
            validateUrlRedirectCount(
                redirects,
                options.policy.maxRedirects
            );
            const QString location =
                response.headers.value(
                    QStringLiteral("location")
                );
            if (location.isEmpty()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "Remote server returned a redirect without a location."
                    ),
                    QStringLiteral("URL_HTTP_ERROR")
                );
            }

            QUrl target = current.resolved(
                QUrl(location, QUrl::StrictMode)
            );
            if (!target.isValid()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "Remote server returned an invalid redirect target."
                    ),
                    QStringLiteral("URL_REDIRECT_BLOCKED")
                );
            }
            validateUrlRedirectTarget(target);
            current = target;
            redirect = true;
            continue;
        }

        if (
            response.status < 200
            || response.status >= 300
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Remote server returned HTTP %1."
                ).arg(response.status),
                QStringLiteral("URL_HTTP_ERROR")
            );
        }

        validateUrlResponseSize(
            contentLength(response.headers),
            static_cast<std::uint64_t>(
                response.body.size()
            ),
            options.policy.maxResponseBytes
        );

        return UrlPayload{
            .bytes = std::move(response.body),
            .mediaType =
                responseMediaType(response.headers),
            .finalUrl = current,
            .headers = std::move(response.headers),
        };
    }
}

} // namespace tcgprint::imports
