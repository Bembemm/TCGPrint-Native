#include "import/ArchidektUrlImport.h"

#include "import/ImportFailure.h"
#include "import/UrlRegistry.h"
#include "import/UrlTransportPolicy.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

namespace tcgprint::imports {
namespace {

inline constexpr qsizetype MaxCards = 100'000;

struct NormalizedCard {
    QString name;
    std::uint64_t quantity{1};
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    std::optional<QString> section;
};

QString hashBytes(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256
        ).toHex()
    );
}

QString deckIdFromUrl(
    const QString& value,
    const QString& sourceId
)
{
    const UrlAdapterResolution resolution =
        resolveUrlAdapter(value);
    if (
        resolution.kind != UrlResolutionKind::Adapter
        || resolution.adapterId
            != QStringLiteral("archidekt")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt URL must point to a public /decks/{numeric-id} page."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }

    const QUrl url(value.trimmed(), QUrl::StrictMode);
    const QRegularExpression pattern(
        QStringLiteral(R"(^/decks/(\d{1,12})/?$)")
    );
    const auto match = pattern.match(url.path());
    if (!match.hasMatch()) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt URL must point to a public /decks/{numeric-id} page."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }
    return match.captured(1);
}

std::optional<QString> textValue(
    const QJsonValue& value
)
{
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        return text.isEmpty()
            ? std::nullopt
            : std::optional<QString>(text);
    }

    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number)) {
            return QString::number(
                number,
                'g',
                15
            );
        }
    }

    return std::nullopt;
}

std::optional<QString> optionalText(
    const QJsonObject& object,
    const QString& key,
    const QString& sourceId
)
{
    const QJsonValue raw = object.value(key);
    if (
        raw.isUndefined()
        || raw.isNull()
        || (
            raw.isString()
            && raw.toString().isEmpty()
        )
    ) {
        return std::nullopt;
    }

    const auto value = textValue(raw);
    if (!value.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "URL adapter payload has an invalid %1 field."
            ).arg(key),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }
    return value;
}

std::uint64_t positiveQuantity(
    const QJsonValue& value,
    const QString& sourceId
)
{
    if (!value.isDouble()) {
        throw ImportFailureError(
            QStringLiteral(
                "URL adapter payload has a missing or invalid card quantity."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const double number = value.toDouble();
    constexpr double MaxSafeInteger =
        9'007'199'254'740'991.0;
    if (
        !std::isfinite(number)
        || number <= 0.0
        || number > MaxSafeInteger
        || std::floor(number) != number
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "URL adapter payload has a missing or invalid card quantity."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    return static_cast<std::uint64_t>(number);
}

QJsonDocument parseJsonPayload(
    const UrlPayload& payload,
    const QString& sourceId
)
{
    if (
        payload.mediaType != QStringLiteral("application/json")
        && !payload.mediaType.endsWith(
            QStringLiteral("+json")
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt API did not return JSON Content-Type."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(payload.bytes);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt API response is not valid UTF-8 JSON."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            text.toUtf8(),
            &error
        );
    if (
        error.error != QJsonParseError::NoError
        || document.isNull()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt API returned malformed JSON."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }
    return document;
}

std::vector<NormalizedCard> normalizeDeck(
    const QJsonDocument& document,
    const QString& sourceId,
    std::optional<QString>& deckName
)
{
    if (!document.isObject()) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt API schema changed: expected a bounded cards array."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const QJsonObject root = document.object();
    const QJsonValue cardsValue =
        root.value(QStringLiteral("cards"));
    if (
        !cardsValue.isArray()
        || cardsValue.toArray().size() > MaxCards
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Archidekt API schema changed: expected a bounded cards array."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    deckName = optionalText(
        root,
        QStringLiteral("name"),
        sourceId
    );

    const QJsonArray cards = cardsValue.toArray();
    std::vector<NormalizedCard> normalized;
    normalized.reserve(
        static_cast<std::size_t>(cards.size())
    );

    for (
        qsizetype index = 0;
        index < cards.size();
        ++index
    ) {
        const QJsonValue itemValue = cards[index];
        if (!itemValue.isObject()) {
            throw ImportFailureError(
                QStringLiteral(
                    "Archidekt API card %1 has no oracle card name."
                ).arg(index + 1),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const QJsonObject row = itemValue.toObject();
        const QJsonValue cardValue =
            row.value(QStringLiteral("card"));
        if (!cardValue.isObject()) {
            throw ImportFailureError(
                QStringLiteral(
                    "Archidekt API card %1 has no oracle card name."
                ).arg(index + 1),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const QJsonObject card = cardValue.toObject();
        const QJsonValue oracleValue =
            card.value(QStringLiteral("oracleCard"));
        if (!oracleValue.isObject()) {
            throw ImportFailureError(
                QStringLiteral(
                    "Archidekt API card %1 has no oracle card name."
                ).arg(index + 1),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const auto name = textValue(
            oracleValue.toObject().value(
                QStringLiteral("name")
            )
        );
        if (!name.has_value()) {
            throw ImportFailureError(
                QStringLiteral(
                    "Archidekt API card %1 has no oracle card name."
                ).arg(index + 1),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const std::uint64_t quantity =
            positiveQuantity(
                row.value(QStringLiteral("quantity")),
                sourceId
            );

        std::optional<QString> section;
        const QJsonValue categoriesValue =
            row.value(QStringLiteral("categories"));
        if (!categoriesValue.isUndefined()) {
            if (!categoriesValue.isArray()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "Archidekt API card %1 has invalid categories."
                    ).arg(index + 1),
                    QStringLiteral("URL_ADAPTER_PAYLOAD"),
                    sourceId
                );
            }

            QStringList categories;
            const QJsonArray rawCategories =
                categoriesValue.toArray();
            for (
                const QJsonValue& category :
                rawCategories
            ) {
                if (!category.isString()) {
                    throw ImportFailureError(
                        QStringLiteral(
                            "Archidekt API card %1 has invalid categories."
                        ).arg(index + 1),
                        QStringLiteral(
                            "URL_ADAPTER_PAYLOAD"
                        ),
                        sourceId
                    );
                }
                const QString trimmed =
                    category.toString().trimmed();
                if (!trimmed.isEmpty()) {
                    categories.push_back(trimmed);
                }
            }
            if (!categories.isEmpty()) {
                section = categories.join(
                    QStringLiteral(", ")
                );
            }
        }

        std::optional<QString> setCode;
        const QJsonValue editionValue =
            card.value(QStringLiteral("edition"));
        if (editionValue.isObject()) {
            setCode = optionalText(
                editionValue.toObject(),
                QStringLiteral("editioncode"),
                sourceId
            );
        }

        normalized.push_back(
            NormalizedCard{
                .name = *name,
                .quantity = quantity,
                .setCode = std::move(setCode),
                .collectorNumber = optionalText(
                    card,
                    QStringLiteral("collectorNumber"),
                    sourceId
                ),
                .section = std::move(section),
            }
        );
    }

    return normalized;
}

QByteArray normalizedBytes(
    const std::vector<NormalizedCard>& cards
)
{
    QJsonArray array;
    for (const NormalizedCard& card : cards) {
        QJsonObject value{
            {
                QStringLiteral("name"),
                card.name
            },
            {
                QStringLiteral("quantity"),
                static_cast<qint64>(card.quantity)
            },
        };
        if (card.setCode.has_value()) {
            value.insert(
                QStringLiteral("set"),
                *card.setCode
            );
        }
        if (card.collectorNumber.has_value()) {
            value.insert(
                QStringLiteral("collectorNumber"),
                *card.collectorNumber
            );
        }
        if (card.section.has_value()) {
            value.insert(
                QStringLiteral("section"),
                *card.section
            );
        }
        array.push_back(value);
    }

    return QJsonDocument(
        QJsonObject{
            {
                QStringLiteral("cards"),
                array
            },
        }
    ).toJson(QJsonDocument::Compact);
}

} // namespace

ArchidektUrlImportResult importArchidektDeckUrl(
    const QString& value,
    const QString& sourceId,
    int order,
    const UrlFetchOptions& fetchOptions
)
{
    if (sourceId.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "Archidekt URL import requires a non-empty source id"
        );
    }

    const QString deckId =
        deckIdFromUrl(value, sourceId);
    const QUrl deckUrl(
        value.trimmed(),
        QUrl::StrictMode
    );
    const QUrl apiUrl(
        QStringLiteral(
            "https://archidekt.com/api/decks/%1/"
        ).arg(deckId)
    );

    const UrlPayload payload =
        fetchUrlPayload(apiUrl, fetchOptions);
    const QJsonDocument document =
        parseJsonPayload(payload, sourceId);

    std::optional<QString> deckName;
    const std::vector<NormalizedCard> cards =
        normalizeDeck(
            document,
            sourceId,
            deckName
        );

    const QByteArray normalized =
        normalizedBytes(cards);
    const QString rawSha =
        hashBytes(payload.bytes);
    const QString normalizedSha =
        hashBytes(normalized);

    ImportSource source{
        .id = sourceId,
        .kind = ImportSourceKind::Url,
        .filename =
            QStringLiteral("archidekt-%1.json")
                .arg(deckId),
        .order = order,
        .originalFormat = QStringLiteral("json"),
        .mediaType =
            QStringLiteral("application/json"),
        .sourceUrl = sanitizeUrlForReport(
            deckUrl.toString(QUrl::FullyEncoded)
        ),
        .adapterId = QStringLiteral("archidekt"),
        .sizeBytes =
            static_cast<std::uint64_t>(
                normalized.size()
            ),
        .originalBytes = normalized,
        .sha256 = normalizedSha,
        .metadata = QJsonObject{
            {
                QStringLiteral("adapterId"),
                QStringLiteral("archidekt")
            },
            {
                QStringLiteral("responseMediaType"),
                payload.mediaType
            },
            {
                QStringLiteral("responseBytes"),
                static_cast<qint64>(
                    payload.bytes.size()
                )
            },
            {
                QStringLiteral("sha256"),
                rawSha
            },
            {
                QStringLiteral("downloadedUrl"),
                sanitizeUrlForReport(
                    payload.finalUrl.toString(
                        QUrl::FullyEncoded
                    )
                )
            },
            {
                QStringLiteral("apiUrl"),
                sanitizeUrlForReport(
                    apiUrl.toString(
                        QUrl::FullyEncoded
                    )
                )
            },
        },
    };
    if (deckName.has_value()) {
        source.metadata.insert(
            QStringLiteral("deckName"),
            *deckName
        );
    }

    ImporterOutput output;
    output.entries.reserve(cards.size());

    for (
        std::size_t index = 0;
        index < cards.size();
        ++index
    ) {
        const NormalizedCard& card = cards[index];
        output.entries.push_back(
            ImportedEntry{
                .id = sourceId
                    + QStringLiteral(":archidekt:")
                    + QString::number(index),
                .kind = ImportedEntryKind::DeckCard,
                .order = static_cast<int>(index),
                .quantity = card.quantity,
                .sourceId = sourceId,
                .sourceFilename = source.filename,
                .cardHint = ImportedCardHint{
                    .name = card.name,
                    .setCode = card.setCode,
                    .collectorNumber =
                        card.collectorNumber,
                    .section = card.section,
                },
                .section = card.section,
                .metadata = QJsonObject{
                    {
                        QStringLiteral("adapterId"),
                        QStringLiteral("archidekt")
                    },
                },
            }
        );
    }

    output.metadata = QJsonObject{
        {
            QStringLiteral("adapterId"),
            QStringLiteral("archidekt")
        },
        {
            QStringLiteral("deckId"),
            deckId
        },
    };
    if (deckName.has_value()) {
        output.metadata.insert(
            QStringLiteral("deckName"),
            *deckName
        );
    }

    return ArchidektUrlImportResult{
        .source = std::move(source),
        .output = std::move(output),
    };
}

} // namespace tcgprint::imports
