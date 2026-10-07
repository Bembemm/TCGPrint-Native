#include "identity/ScryfallIdentityClient.h"

#include "import/ImportFailure.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringDecoder>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace tcgprint::identity {
namespace {

constexpr std::uint64_t MaxScryfallIdentityJsonBytes =
    8ULL * 1024ULL * 1024ULL;

const QString ScryfallApiOrigin =
    QStringLiteral("https://api.scryfall.com");
const QString ScryfallAccept =
    QStringLiteral("application/json;q=0.9,*/*;q=0.8");
const QString ScryfallUserAgent =
    QStringLiteral(
        "TCGPrint/0.1.0 (+https://github.com/Bembemm/TCGPrint)"
    );

QUrl urlFromEncodedPath(const QByteArray& path)
{
    return QUrl::fromEncoded(
        ScryfallApiOrigin.toLatin1() + path,
        QUrl::StrictMode
    );
}

QByteArray encodePathSegment(const QString& value)
{
    return QUrl::toPercentEncoding(value);
}

ScryfallIdentityLookupError networkError()
{
    return ScryfallIdentityLookupError(
        ScryfallIdentityLookupErrorKind::Network,
        "Scryfall request could not be completed."
    );
}

} // namespace

ScryfallIdentityLookupError::ScryfallIdentityLookupError(
    ScryfallIdentityLookupErrorKind kind,
    const char* message,
    std::optional<int> status
)
    : std::runtime_error(message)
    , kind_(kind)
    , status_(status)
{
}

ScryfallIdentityLookupErrorKind
ScryfallIdentityLookupError::kind() const noexcept
{
    return kind_;
}

std::optional<int> ScryfallIdentityLookupError::status() const noexcept
{
    return status_;
}

ScryfallIdentityClient::ScryfallIdentityClient(
    imports::UrlFetchOptions transportOptions
)
    : transportOptions_(std::move(transportOptions))
{
    transportOptions_.policy.maxResponseBytes = std::min(
        transportOptions_.policy.maxResponseBytes,
        MaxScryfallIdentityJsonBytes
    );
    transportOptions_.policy.maxRedirects = 0;
}

ScryfallIdentityCard ScryfallIdentityClient::lookupById(
    const QString& scryfallId
) const
{
    QByteArray path = QByteArrayLiteral("/cards/");
    path += encodePathSegment(scryfallId);
    return lookup(urlFromEncodedPath(path));
}

ScryfallIdentityCard ScryfallIdentityClient::lookupBySetCollector(
    const QString& setCode,
    const QString& collectorNumber,
    const std::optional<QString>& language
) const
{
    QByteArray path = QByteArrayLiteral("/cards/");
    path += encodePathSegment(setCode);
    path += '/';
    path += encodePathSegment(collectorNumber);
    if (language.has_value() && !language->isEmpty()) {
        path += '/';
        path += encodePathSegment(*language);
    }
    return lookup(urlFromEncodedPath(path));
}

ScryfallIdentityCard ScryfallIdentityClient::lookupByExactName(
    const QString& name
) const
{
    QByteArray requestTarget = QByteArrayLiteral("/cards/named?exact=");
    requestTarget += encodePathSegment(name);
    return lookup(urlFromEncodedPath(requestTarget));
}

ScryfallIdentityCard ScryfallIdentityClient::lookup(
    const QUrl& url
) const
{
    if (
        !url.isValid()
        || url.scheme() != QStringLiteral("https")
        || url.host() != QStringLiteral("api.scryfall.com")
        || !url.userName().isEmpty()
        || !url.password().isEmpty()
        || url.port(-1) != -1
    ) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::Network,
            "Scryfall requests must use the official HTTPS API host."
        );
    }

    imports::UrlFetchOptions options = transportOptions_;
    options.requestHeaders = {
        {
            QStringLiteral("accept"),
            ScryfallAccept,
        },
        {
            QStringLiteral("user-agent"),
            ScryfallUserAgent,
        },
    };

    imports::UrlFetchResponse response;
    try {
        response = imports::fetchUrlResponse(url, options);
    } catch (const imports::ImportFailureError& error) {
        if (error.code() == QStringLiteral("URL_TIMEOUT")) {
            throw ScryfallIdentityLookupError(
                ScryfallIdentityLookupErrorKind::Timeout,
                "Scryfall request timed out."
            );
        }
        if (error.code() == QStringLiteral("URL_RESPONSE_LIMIT")) {
            throw ScryfallIdentityLookupError(
                ScryfallIdentityLookupErrorKind::ResponseTooLarge,
                "Scryfall response exceeds the configured byte limit."
            );
        }
        throw networkError();
    } catch (const std::exception&) {
        throw networkError();
    }

    if (response.status == 404) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::NotFound,
            "Scryfall did not find the requested card.",
            response.status
        );
    }
    if (response.status == 429) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::RateLimited,
            "Scryfall rate limited the request.",
            response.status
        );
    }
    if (response.status >= 500 && response.status <= 599) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::Server,
            "Scryfall returned a server error.",
            response.status
        );
    }
    if (response.status < 200 || response.status >= 300) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::Http,
            "Scryfall returned an HTTP error.",
            response.status
        );
    }

    if (response.mediaType != QStringLiteral("application/json")) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::InvalidContentType,
            "Scryfall returned an incompatible content type."
        );
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(response.bytes);
    if (decoder.hasError()) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::InvalidJson,
            "Scryfall returned invalid JSON."
        );
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        decoded.toUtf8(),
        &parseError
    );
    if (parseError.error != QJsonParseError::NoError) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::InvalidJson,
            "Scryfall returned invalid JSON."
        );
    }
    if (!document.isObject()) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::InvalidPayload,
            "Scryfall returned an invalid card payload."
        );
    }

    try {
        return parseScryfallIdentityCard(document.object());
    } catch (const ScryfallIdentityPayloadError&) {
        throw ScryfallIdentityLookupError(
            ScryfallIdentityLookupErrorKind::InvalidPayload,
            "Scryfall returned an invalid card payload."
        );
    }
}

} // namespace tcgprint::identity
