#pragma once

#include "identity/ScryfallIdentity.h"
#include "import/UrlHttpTransport.h"

#include <QString>
#include <QUrl>

#include <optional>
#include <stdexcept>

namespace tcgprint::identity {

enum class ScryfallIdentityLookupErrorKind {
    NotFound,
    RateLimited,
    Server,
    Http,
    Network,
    Timeout,
    InvalidJson,
    InvalidPayload,
    InvalidContentType,
    ResponseTooLarge,
};

class ScryfallIdentityLookupError final : public std::runtime_error
{
public:
    ScryfallIdentityLookupError(
        ScryfallIdentityLookupErrorKind kind,
        const char* message,
        std::optional<int> status = std::nullopt
    );

    [[nodiscard]] ScryfallIdentityLookupErrorKind kind() const noexcept;
    [[nodiscard]] std::optional<int> status() const noexcept;

private:
    ScryfallIdentityLookupErrorKind kind_;
    std::optional<int> status_;
};

class ScryfallIdentityClient final
{
public:
    explicit ScryfallIdentityClient(
        imports::UrlFetchOptions transportOptions = {}
    );

    [[nodiscard]] ScryfallIdentityCard lookupById(
        const QString& scryfallId
    ) const;

    [[nodiscard]] ScryfallIdentityCard lookupBySetCollector(
        const QString& setCode,
        const QString& collectorNumber,
        const std::optional<QString>& language = std::nullopt
    ) const;

    [[nodiscard]] ScryfallIdentityCard lookupByExactName(
        const QString& name
    ) const;

private:
    [[nodiscard]] ScryfallIdentityCard lookup(
        const QUrl& url
    ) const;

    imports::UrlFetchOptions transportOptions_;
};

} // namespace tcgprint::identity
