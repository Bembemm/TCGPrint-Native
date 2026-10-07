#pragma once

#include "identity/ScryfallIdentityLookup.h"

namespace tcgprint::identity {

class IdentityResolver final
{
public:
    // The lookup must outlive this resolver.
    explicit IdentityResolver(const ScryfallIdentityLookup& lookup);

    // Returns a copy; lookup failures other than NotFound propagate unchanged.
    [[nodiscard]] cards::WorkingCard resolve(
        const cards::WorkingCard& workingCard
    ) const;

private:
    const ScryfallIdentityLookup& lookup_;
};

} // namespace tcgprint::identity
