#include "import/UrlTransportPolicy.h"

#include "import/ImportFailure.h"

#include <QHostAddress>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <stdexcept>

namespace tcgprint::imports {
namespace {

QString blockedCode(bool redirect)
{
    return redirect
        ? QStringLiteral("URL_REDIRECT_BLOCKED")
        : QStringLiteral("URL_HOST_BLOCKED");
}

bool ipv4Public(quint32 value)
{
    const int a = static_cast<int>((value >> 24) & 0xffU);
    const int b = static_cast<int>((value >> 16) & 0xffU);
    const int c = static_cast<int>((value >> 8) & 0xffU);

    if (
        a == 0
        || a == 10
        || a == 127
        || a >= 224
    ) {
        return false;
    }
    if (a == 100 && b >= 64 && b <= 127) {
        return false;
    }
    if (a == 169 && b == 254) {
        return false;
    }
    if (a == 172 && b >= 16 && b <= 31) {
        return false;
    }
    if (
        a == 192
        && (
            (b == 0 && (c == 0 || c == 2))
            || b == 168
        )
    ) {
        return false;
    }
    if (a == 192 && b == 88 && c == 99) {
        return false;
    }
    if (
        a == 198
        && (
            b == 18
            || b == 19
            || (b == 51 && c == 100)
        )
    ) {
        return false;
    }
    if (a == 203 && b == 0 && c == 113) {
        return false;
    }

    return true;
}

bool ipv6Public(const Q_IPV6ADDR& bytes)
{
    const bool mappedV4 =
        std::all_of(
            bytes.c,
            bytes.c + 10,
            [](quint8 byte) {
                return byte == 0;
            }
        )
        && bytes.c[10] == 0xff
        && bytes.c[11] == 0xff;

    if (mappedV4) {
        const quint32 ipv4 =
            (static_cast<quint32>(bytes.c[12]) << 24)
            | (static_cast<quint32>(bytes.c[13]) << 16)
            | (static_cast<quint32>(bytes.c[14]) << 8)
            | static_cast<quint32>(bytes.c[15]);
        return ipv4Public(ipv4);
    }

    const quint16 first =
        static_cast<quint16>(
            (static_cast<quint16>(bytes.c[0]) << 8)
            | bytes.c[1]
        );
    const quint16 second =
        static_cast<quint16>(
            (static_cast<quint16>(bytes.c[2]) << 8)
            | bytes.c[3]
        );

    if (first < 0x2000 || first > 0x3fff) {
        return false;
    }
    if (first == 0x2001 && second == 0x0db8) {
        return false;
    }
    if (first == 0x2001 && second <= 0x01ff) {
        return false;
    }
    if (first == 0x2002 || first == 0x3fff) {
        return false;
    }

    return true;
}

bool blockedHostname(const QString& hostname)
{
    const QString host = hostname.toLower();
    return host.isEmpty()
        || host == QStringLiteral("localhost")
        || host.endsWith(QStringLiteral(".localhost"))
        || host.endsWith(QStringLiteral(".local"))
        || host.endsWith(QStringLiteral(".internal"));
}

} // namespace

void validateUrlTransportPolicy(
    const UrlTransportPolicy& policy
)
{
    if (
        policy.timeoutMs < 1
        || policy.maxResponseBytes < 1
        || policy.maxRedirects < 0
    ) {
        throw std::invalid_argument(
            "URL transport limits must be positive"
        );
    }
}

QString sanitizeUrlForReport(const QString& value)
{
    QUrl url(value, QUrl::StrictMode);
    if (
        !url.isValid()
        || url.scheme().isEmpty()
        || url.host().isEmpty()
    ) {
        return QStringLiteral("[invalid URL]");
    }

    url.setUserName(QString());
    url.setPassword(QString());
    url.setFragment(QString());

    const QRegularExpression sensitive(
        QStringLiteral(
            R"((?:password|passwd|token|secret|signature|^sig$|auth|api[_-]?key|access[_-]?key|credential|session|bearer))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );

    QUrlQuery query(url);
    auto items = query.queryItems(QUrl::FullyDecoded);
    for (auto& item : items) {
        if (sensitive.match(item.first).hasMatch()) {
            item.second = QStringLiteral("[redacted]");
        }
    }
    query.setQueryItems(items);
    url.setQuery(query);

    return url.toString(QUrl::FullyEncoded);
}

bool isPublicUrlAddress(const QString& address)
{
    QHostAddress parsed;
    if (!parsed.setAddress(address)) {
        return false;
    }

    if (
        parsed.protocol()
        == QAbstractSocket::IPv4Protocol
    ) {
        bool ok = false;
        const quint32 value =
            parsed.toIPv4Address(&ok);
        return ok && ipv4Public(value);
    }

    if (
        parsed.protocol()
        == QAbstractSocket::IPv6Protocol
    ) {
        return ipv6Public(
            parsed.toIPv6Address()
        );
    }

    return false;
}

void validateResolvedUrlHost(
    const QUrl& url,
    const QStringList& resolvedAddresses,
    bool redirect
)
{
    const QString code = blockedCode(redirect);
    const QString hostname =
        url.host().toLower();

    if (blockedHostname(hostname)) {
        throw ImportFailureError(
            QStringLiteral(
                "URL points to a local or reserved host, which is blocked."
            ),
            code
        );
    }

    QHostAddress directAddress;
    if (directAddress.setAddress(hostname)) {
        if (!isPublicUrlAddress(hostname)) {
            throw ImportFailureError(
                QStringLiteral(
                    "URL points to a private or reserved IP address, which is blocked."
                ),
                code
            );
        }
        return;
    }

    if (
        resolvedAddresses.isEmpty()
        || std::any_of(
            resolvedAddresses.begin(),
            resolvedAddresses.end(),
            [](const QString& address) {
                return !isPublicUrlAddress(address);
            }
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "URL resolves to a private or reserved IP address, which is blocked."
            ),
            code
        );
    }
}

void validateUrlRedirectTarget(const QUrl& url)
{
    if (
        url.scheme() != QStringLiteral("http")
        && url.scheme() != QStringLiteral("https")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote redirects must use HTTP or HTTPS."
            ),
            QStringLiteral("URL_REDIRECT_BLOCKED")
        );
    }

    if (
        url.host().isEmpty()
        || !url.userName().isEmpty()
        || !url.password().isEmpty()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote URL contains an invalid host or embedded credentials."
            ),
            QStringLiteral("URL_REDIRECT_BLOCKED")
        );
    }
}

void validateUrlRedirectCount(
    int redirectsAlreadyFollowed,
    int maximum
)
{
    if (
        maximum < 0
        || redirectsAlreadyFollowed >= maximum
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote response exceeded the redirect limit."
            ),
            QStringLiteral("URL_REDIRECT_BLOCKED")
        );
    }
}

void validateUrlResponseSize(
    std::optional<std::uint64_t> contentLength,
    std::uint64_t receivedBytes,
    std::uint64_t maximum
)
{
    if (
        maximum < 1
        || (
            contentLength.has_value()
            && *contentLength > maximum
        )
        || receivedBytes > maximum
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Remote response exceeds the configured byte limit."
            ),
            QStringLiteral("URL_RESPONSE_LIMIT")
        );
    }
}

} // namespace tcgprint::imports
