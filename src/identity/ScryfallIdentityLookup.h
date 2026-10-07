#pragma once

#include "identity/ScryfallIdentity.h"

namespace tcgprint::identity {

// Strong identity lookups only; production transport remains in the client.
class ScryfallIdentityLookup
{
public:
    virtual ~ScryfallIdentityLookup() = default;

    [[nodiscard]] virtual ScryfallIdentityCard lookupById(
        const QString& scryfallId
    ) const = 0;

    [[nodiscard]] virtual ScryfallIdentityCard lookupBySetCollector(
        const QString& setCode,
        const QString& collectorNumber,
        const std::optional<QString>& language = std::nullopt
    ) const = 0;

    [[nodiscard]] virtual ScryfallIdentityCard lookupByExactName(
        const QString& name
    ) const = 0;
};

} // namespace tcgprint::identity
