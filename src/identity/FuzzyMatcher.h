#pragma once

#include "identity/IdentityResolutionPolicy.h"

#include <QString>

#include <optional>
#include <vector>

namespace tcgprint::identity {

struct FuzzyCandidate final
{
    QString name;
    std::optional<QString> id;

    bool operator==(const FuzzyCandidate&) const = default;
};

enum class FuzzyMatchStatus { Resolved, Suggested, Ambiguous, Unresolved };
enum class FuzzyMatchReason { ExactName, ClosestName, CompetingCloseMatches, BelowThreshold };

struct RankedFuzzyCandidate final
{
    FuzzyCandidate candidate;
    double score;

    bool operator==(const RankedFuzzyCandidate&) const = default;
};

struct FuzzyMatch final
{
    FuzzyMatchStatus status{FuzzyMatchStatus::Unresolved};
    double score{0};
    FuzzyMatchReason reason{FuzzyMatchReason::BelowThreshold};
    std::optional<FuzzyCandidate> candidate;
    std::vector<RankedFuzzyCandidate> candidates;

    bool operator==(const FuzzyMatch&) const = default;
};

// Pure value operation. Query lengths/score denominators use UTF-16 units like
// JavaScript; edit distance uses Unicode code points, never UTF-8 bytes.
[[nodiscard]] FuzzyMatch fuzzyMatchName(
    const QString& query,
    const std::vector<FuzzyCandidate>& candidates,
    const IdentityResolutionPolicy& policy = IdentityResolutionPolicyDefault
);

} // namespace tcgprint::identity
