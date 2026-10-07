#pragma once

#include <QUrl>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace tcgprint::imports {

inline constexpr std::uint64_t DefaultUrlTimeoutMs = 10'000;
inline constexpr std::uint64_t DefaultMaxUrlResponseBytes =
    25ULL * 1024ULL * 1024ULL;
inline constexpr int DefaultMaxUrlRedirects = 5;

struct UrlTransportPolicy {
    std::uint64_t timeoutMs{DefaultUrlTimeoutMs};
    std::uint64_t maxResponseBytes{DefaultMaxUrlResponseBytes};
    int maxRedirects{DefaultMaxUrlRedirects};
};

void validateUrlTransportPolicy(
    const UrlTransportPolicy& policy
);

QString sanitizeUrlForReport(const QString& value);

bool isPublicUrlAddress(const QString& address);

void validateResolvedUrlHost(
    const QUrl& url,
    const QStringList& resolvedAddresses,
    bool redirect = false
);

void validateUrlRedirectTarget(const QUrl& url);

void validateUrlRedirectCount(
    int redirectsAlreadyFollowed,
    int maximum = DefaultMaxUrlRedirects
);

void validateUrlResponseSize(
    std::optional<std::uint64_t> contentLength,
    std::uint64_t receivedBytes,
    std::uint64_t maximum
);

} // namespace tcgprint::imports
