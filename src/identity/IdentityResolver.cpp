#include "identity/IdentityResolver.h"

#include "identity/ScryfallIdentityClient.h"

namespace tcgprint::identity {
namespace {

bool hasHint(const std::optional<std::string>& hint)
{
    return hint.has_value() && !hint->empty();
}

template<typename Lookup>
std::optional<ScryfallIdentityCard> lookupOrNotFound(Lookup lookup)
{
    try {
        return lookup();
    } catch (const ScryfallIdentityLookupError& error) {
        if (error.kind() != ScryfallIdentityLookupErrorKind::NotFound) {
            throw;
        }
        return std::nullopt;
    }
}

cards::WorkingCard resolved(
    const cards::WorkingCard& workingCard,
    const ScryfallIdentityCard& printing,
    cards::IdentityResolutionMethod method
)
{
    cards::WorkingCard result = workingCard;
    result.identity = toCardIdentity(printing, method, 1.0);
    result.identityResolution = {
        .status = cards::IdentityResolutionStatus::Resolved,
        .method = method,
        .query = workingCard.identityHints.name,
        .confidence = 1.0,
        .candidates = {},
        .confirmed = false,
    };
    return result;
}

} // namespace

IdentityResolver::IdentityResolver(const ScryfallIdentityLookup& lookup)
    : lookup_(lookup)
{
}

cards::WorkingCard IdentityResolver::resolve(
    const cards::WorkingCard& workingCard
) const
{
    if (workingCard.identityResolution.confirmed) {
        return workingCard;
    }

    const auto& source = workingCard.importSource;
    const bool isCustom = source.entryKind == "custom-card"
        || source.entryKind == "asset";
    if (isCustom && source.identityHintOrigin != "explicit-card-hint") {
        if (workingCard.identity) {
            return workingCard;
        }
        cards::WorkingCard result = workingCard;
        result.identity.reset();
        result.identityResolution = {
            .status = cards::IdentityResolutionStatus::Custom,
            .candidates = {},
            .confirmed = true,
        };
        return result;
    }

    const auto& hints = workingCard.identityHints;
    if (hasHint(hints.scryfallId)) {
        const auto printing = lookupOrNotFound([&] {
            return lookup_.lookupById(QString::fromStdString(*hints.scryfallId));
        });
        if (printing) {
            return resolved(
                workingCard, *printing, cards::IdentityResolutionMethod::ScryfallId
            );
        }
    }

    if (hasHint(hints.setCode) && hasHint(hints.collectorNumber)) {
        const auto printing = lookupOrNotFound([&] {
            const std::optional<QString> language = hints.language
                ? std::optional<QString>(QString::fromStdString(*hints.language))
                : std::nullopt;
            return lookup_.lookupBySetCollector(
                QString::fromStdString(*hints.setCode),
                QString::fromStdString(*hints.collectorNumber),
                language
            );
        });
        if (printing) {
            return resolved(
                workingCard, *printing, cards::IdentityResolutionMethod::SetCollector
            );
        }
    }

    if (hasHint(hints.name)) {
        const auto printing = lookupOrNotFound([&] {
            return lookup_.lookupByExactName(QString::fromStdString(*hints.name));
        });
        if (printing) {
            return resolved(
                workingCard, *printing, cards::IdentityResolutionMethod::Name
            );
        }
    }

    cards::WorkingCard result = workingCard;
    result.identity.reset();
    result.identityResolution = {
        .status = cards::IdentityResolutionStatus::Unresolved,
        .query = hints.name,
        .candidates = {},
        .confirmed = false,
    };
    return result;
}

} // namespace tcgprint::identity
