#pragma once

#include <cstddef>

namespace tcgprint::identity {

struct IdentityResolutionPolicy final
{
    double fuzzySuggestThreshold;
    double ambiguousMargin;
    std::size_t minimumQueryLength;
    std::size_t maximumCandidates;
};

// Shared resolver thresholds: customize a copy, never duplicate defaults at call sites.
inline constexpr IdentityResolutionPolicy IdentityResolutionPolicyDefault{
    .fuzzySuggestThreshold = 0.72,
    .ambiguousMargin = 0.04,
    .minimumQueryLength = 2,
    .maximumCandidates = 5,
};

} // namespace tcgprint::identity
