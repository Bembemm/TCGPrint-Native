#include "identity/ScryfallIdentity.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <string>

namespace tcgprint::identity {
namespace {

[[nodiscard]] const QJsonObject object(
    const QJsonValue& value,
    const QString& label
)
{
    if (!value.isObject()) {
        throw ScryfallIdentityPayloadError(
            QStringLiteral(
                "Scryfall %1 must be an object."
            ).arg(label).toStdString()
        );
    }
    return value.toObject();
}

[[nodiscard]] QString requiredString(
    const QJsonObject& source,
    const QString& key,
    const QString& label
)
{
    const QJsonValue value = source.value(key);
    if (
        !value.isString()
        || value.toString().isEmpty()
    ) {
        throw ScryfallIdentityPayloadError(
            QStringLiteral(
                "Scryfall card is missing %1."
            ).arg(label).toStdString()
        );
    }
    return value.toString();
}

[[nodiscard]] std::optional<QString> optionalString(
    const QJsonObject& source,
    const QString& key
)
{
    const QJsonValue value = source.value(key);
    if (value.isUndefined() || value.isNull()) {
        return std::nullopt;
    }
    if (!value.isString()) {
        throw ScryfallIdentityPayloadError(
            QStringLiteral(
                "Scryfall field %1 must be a string."
            ).arg(key).toStdString()
        );
    }
    return value.toString();
}

[[nodiscard]] std::optional<bool> optionalBoolean(
    const QJsonObject& source,
    const QString& key
)
{
    const QJsonValue value = source.value(key);
    if (value.isUndefined() || value.isNull()) {
        return std::nullopt;
    }
    if (!value.isBool()) {
        throw ScryfallIdentityPayloadError(
            QStringLiteral(
                "Scryfall field %1 must be a boolean."
            ).arg(key).toStdString()
        );
    }
    return value.toBool();
}

[[nodiscard]] std::string utf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size())
    );
}

[[nodiscard]] std::optional<std::string> utf8(
    const std::optional<QString>& value
)
{
    if (!value.has_value()) {
        return std::nullopt;
    }
    return utf8(*value);
}

} // namespace

ScryfallIdentityCard parseScryfallIdentityCard(
    const QJsonValue& value
)
{
    const QJsonObject card =
        object(value, QStringLiteral("card"));

    ScryfallIdentityCard result{
        .id = requiredString(
            card,
            QStringLiteral("id"),
            QStringLiteral("id")
        ),
        .oracleId = optionalString(
            card,
            QStringLiteral("oracle_id")
        ),
        .name = requiredString(
            card,
            QStringLiteral("name"),
            QStringLiteral("name")
        ),
        .layout = requiredString(
            card,
            QStringLiteral("layout"),
            QStringLiteral("layout")
        ),
        .setCode = optionalString(
            card,
            QStringLiteral("set")
        ),
        .collectorNumber = optionalString(
            card,
            QStringLiteral("collector_number")
        ),
        .lang = optionalString(
            card,
            QStringLiteral("lang")
        ),
        .digital = optionalBoolean(
            card,
            QStringLiteral("digital")
        ),
        .promo = optionalBoolean(
            card,
            QStringLiteral("promo")
        ),
        .fullArt = optionalBoolean(
            card,
            QStringLiteral("full_art")
        ),
        .imageStatus = optionalString(
            card,
            QStringLiteral("image_status")
        ),
    };

    const QJsonValue facesValue =
        card.value(QStringLiteral("card_faces"));
    if (!facesValue.isUndefined()) {
        if (!facesValue.isArray()) {
            throw ScryfallIdentityPayloadError(
                "Scryfall card_faces must be an array."
            );
        }

        const QJsonArray faces = facesValue.toArray();
        result.faceNames.reserve(
            static_cast<std::size_t>(faces.size())
        );
        for (
            qsizetype index = 0;
            index < faces.size();
            ++index
        ) {
            const QJsonObject face = object(
                faces[index],
                QStringLiteral("card_faces[%1]")
                    .arg(index)
            );
            result.faceNames.push_back(
                requiredString(
                    face,
                    QStringLiteral("name"),
                    QStringLiteral(
                        "card_faces[%1].name"
                    ).arg(index)
                )
            );
        }
    }

    const QJsonValue relatedValue =
        card.value(QStringLiteral("all_parts"));
    if (!relatedValue.isUndefined() && !relatedValue.isNull()) {
        if (!relatedValue.isArray()) {
            throw ScryfallIdentityPayloadError(
                "Scryfall all_parts must be an array."
            );
        }

        const QJsonArray related = relatedValue.toArray();
        result.relatedCards.reserve(
            static_cast<std::size_t>(related.size())
        );
        for (
            qsizetype index = 0;
            index < related.size();
            ++index
        ) {
            const QJsonObject item = object(
                related[index],
                QStringLiteral("all_parts[%1]")
                    .arg(index)
            );
            result.relatedCards.push_back(
                ScryfallRelatedCard{
                    .id = requiredString(
                        item,
                        QStringLiteral("id"),
                        QStringLiteral("all_parts[%1].id")
                            .arg(index)
                    ),
                    .component = requiredString(
                        item,
                        QStringLiteral("component"),
                        QStringLiteral(
                            "all_parts[%1].component"
                        ).arg(index)
                    ),
                    .name = requiredString(
                        item,
                        QStringLiteral("name"),
                        QStringLiteral("all_parts[%1].name")
                            .arg(index)
                    ),
                    .typeLine = optionalString(
                        item,
                        QStringLiteral("type_line")
                    ),
                }
            );
        }
    }

    return result;
}

cards::CardIdentity toCardIdentity(
    const ScryfallIdentityCard& card,
    cards::IdentityResolutionMethod method,
    double confidence
)
{
    if (
        card.id.isEmpty()
        || card.name.isEmpty()
        || card.layout.isEmpty()
    ) {
        throw std::invalid_argument(
            "Scryfall identity requires id, name and layout"
        );
    }
    if (
        !std::isfinite(confidence)
        || confidence < 0.0
        || confidence > 1.0
    ) {
        throw std::invalid_argument(
            "Scryfall identity confidence must be between 0 and 1"
        );
    }

    cards::CardIdentityMetadata metadata;
    metadata.scalars.emplace(
        "layout",
        utf8(card.layout)
    );
    metadata.scalars.emplace(
        "digital",
        card.digital.value_or(false)
    );
    metadata.scalars.emplace(
        "promo",
        card.promo.value_or(false)
    );
    metadata.scalars.emplace(
        "fullArt",
        card.fullArt.value_or(false)
    );
    if (card.imageStatus.has_value()) {
        metadata.scalars.emplace(
            "imageStatus",
            utf8(*card.imageStatus)
        );
    }

    metadata.faces.reserve(card.faceNames.size());
    for (const QString& face : card.faceNames) {
        metadata.faces.push_back(
            cards::IdentityMetadataFace{
                .name = utf8(face),
            }
        );
    }

    metadata.relatedCards.reserve(
        card.relatedCards.size()
    );
    for (
        const ScryfallRelatedCard& related :
        card.relatedCards
    ) {
        metadata.relatedCards.push_back(
            cards::IdentityMetadataRelatedCard{
                .id = utf8(related.id),
                .name = utf8(related.name),
                .component = utf8(related.component),
                .typeLine = utf8(related.typeLine),
            }
        );
    }

    const std::string scryfallId = utf8(card.id);
    const std::optional<std::string> oracleId =
        utf8(card.oracleId);

    return cards::CardIdentity{
        .provider = "scryfall",
        .id = oracleId.has_value()
            ? "scryfall:oracle:" + *oracleId
            : "scryfall:card:" + scryfallId,
        .name = utf8(card.name),
        .scryfallId = scryfallId,
        .oracleId = oracleId,
        .setCode = utf8(card.setCode),
        .collectorNumber =
            utf8(card.collectorNumber),
        .lang = utf8(card.lang),
        .resolutionMethod = method,
        .confidence = confidence,
        .metadata = std::move(metadata),
    };
}

} // namespace tcgprint::identity
