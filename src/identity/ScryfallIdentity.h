#pragma once

#include "domain/cards/WorkingCard.h"

#include <QJsonValue>
#include <QString>

#include <optional>
#include <vector>

namespace tcgprint::identity {

struct ScryfallRelatedCard final
{
    QString id;
    QString component;
    QString name;
    std::optional<QString> typeLine;
};

struct ScryfallIdentityCard final
{
    QString id;
    std::optional<QString> oracleId;
    QString name;
    QString layout;
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    std::optional<QString> lang;
    std::optional<bool> digital;
    std::optional<bool> promo;
    std::optional<bool> fullArt;
    std::optional<QString> imageStatus;
    std::vector<QString> faceNames;
    std::vector<ScryfallRelatedCard> relatedCards;
};

class ScryfallIdentityPayloadError final : public std::runtime_error
{
public:
    explicit ScryfallIdentityPayloadError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

[[nodiscard]] ScryfallIdentityCard parseScryfallIdentityCard(
    const QJsonValue& value
);

[[nodiscard]] cards::CardIdentity toCardIdentity(
    const ScryfallIdentityCard& card,
    cards::IdentityResolutionMethod method,
    double confidence
);

} // namespace tcgprint::identity
