#include "import/CubeCobraUrlImport.h"

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

inline constexpr std::size_t MaxCards = 100'000;

struct BoardSpec {
    const char* key;
    const char* section;
    bool required;
};

constexpr BoardSpec Boards[]{
    {"mainboard", "Mainboard", true},
    {"maybeboard", "Maybeboard", false},
    {"basics", "Basics", false},
};

struct NormalizedCard {
    QString name;
    std::uint64_t quantity{1};
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    std::optional<QString> scryfallId;
    QString section;
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

QString cubeIdFromUrl(
    const QString& value,
    const QString& sourceId
)
{
    const UrlAdapterResolution resolution =
        resolveUrlAdapter(value);
    if (
        resolution.kind != UrlResolutionKind::Adapter
        || resolution.adapterId
            != QStringLiteral("cubecobra")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "CubeCobra URL must point to /cube/overview/{cube-id}."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }

    const QUrl url(value.trimmed(), QUrl::StrictMode);
    const QRegularExpression pattern(
        QStringLiteral(
            R"(^/cube/overview/([a-z0-9_-]{2,80})/?$)"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = pattern.match(url.path());
    if (!match.hasMatch()) {
        throw ImportFailureError(
            QStringLiteral(
                "CubeCobra URL must point to /cube/overview/{cube-id}."
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
            return QString::number(number, 'g', 15);
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
                "CubeCobra API did not return JSON Content-Type."
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
                "CubeCobra API response is not valid UTF-8 JSON."
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
                "CubeCobra API returned malformed JSON."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }
    return document;
}

std::vector<NormalizedCard> normalizeCube(
    const QJsonDocument& document,
    const QString& sourceId,
    std::optional<QString>& cubeName
)
{
    if (!document.isObject()) {
        throw ImportFailureError(
            QStringLiteral(
                "CubeCobra API schema changed: expected a cards object."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const QJsonObject root = document.object();
    const QJsonValue cardsValue =
        root.value(QStringLiteral("cards"));
    if (!cardsValue.isObject()) {
        throw ImportFailureError(
            QStringLiteral(
                "CubeCobra API schema changed: expected a cards object."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    cubeName = optionalText(
        root,
        QStringLiteral("name"),
        sourceId
    );

    const QJsonObject cubeCards =
        cardsValue.toObject();
    std::vector<NormalizedCard> normalized;

    for (const BoardSpec& board : Boards) {
        const QString key =
            QString::fromLatin1(board.key);
        const QJsonValue boardValue =
            cubeCards.value(key);

        if (
            boardValue.isUndefined()
            && !board.required
        ) {
            continue;
        }

        if (!boardValue.isArray()) {
            throw ImportFailureError(
                QStringLiteral(
                    "CubeCobra API board %1 must be an array."
                ).arg(key),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const QJsonArray rows =
            boardValue.toArray();
        for (
            qsizetype index = 0;
            index < rows.size();
            ++index
        ) {
            const QJsonValue rowValue = rows[index];
            if (!rowValue.isObject()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "CubeCobra API %1 card %2 has no details.name."
                    ).arg(key).arg(index + 1),
                    QStringLiteral("URL_ADAPTER_PAYLOAD"),
                    sourceId
                );
            }

            const QJsonObject row =
                rowValue.toObject();
            const QJsonValue detailsValue =
                row.value(QStringLiteral("details"));
            if (!detailsValue.isObject()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "CubeCobra API %1 card %2 has no details.name."
                    ).arg(key).arg(index + 1),
                    QStringLiteral("URL_ADAPTER_PAYLOAD"),
                    sourceId
                );
            }

            const QJsonObject details =
                detailsValue.toObject();
            const auto name = optionalText(
                details,
                QStringLiteral("name"),
                sourceId
            );
            if (!name.has_value()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "CubeCobra API %1 card %2 has no details.name."
                    ).arg(key).arg(index + 1),
                    QStringLiteral("URL_ADAPTER_PAYLOAD"),
                    sourceId
                );
            }

            QJsonValue quantityValue =
                row.value(QStringLiteral("quantity"));
            if (quantityValue.isUndefined()) {
                quantityValue =
                    row.value(QStringLiteral("count"));
            }
            if (quantityValue.isUndefined()) {
                quantityValue =
                    row.value(QStringLiteral("qty"));
            }

            const std::uint64_t quantity =
                quantityValue.isUndefined()
                ? 1
                : positiveQuantity(
                    quantityValue,
                    sourceId
                );

            normalized.push_back(
                NormalizedCard{
                    .name = *name,
                    .quantity = quantity,
                    .setCode = optionalText(
                        details,
                        QStringLiteral("set"),
                        sourceId
                    ),
                    .collectorNumber = optionalText(
                        details,
                        QStringLiteral(
                            "collector_number"
                        ),
                        sourceId
                    ),
                    .scryfallId = optionalText(
                        details,
                        QStringLiteral("scryfall_id"),
                        sourceId
                    ),
                    .section =
                        QString::fromLatin1(
                            board.section
                        ),
                }
            );

            if (normalized.size() > MaxCards) {
                throw ImportFailureError(
                    QStringLiteral(
                        "CubeCobra API exceeded the card count limit."
                    ),
                    QStringLiteral("URL_ADAPTER_PAYLOAD"),
                    sourceId
                );
            }
        }
    }

    if (normalized.empty()) {
        throw ImportFailureError(
            QStringLiteral(
                "CubeCobra API returned no cards in its supported boards."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
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
            {
                QStringLiteral("section"),
                card.section
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
        if (card.scryfallId.has_value()) {
            value.insert(
                QStringLiteral("scryfallId"),
                *card.scryfallId
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

CubeCobraUrlImportResult importCubeCobraUrl(
    const QString& value,
    const QString& sourceId,
    int order,
    const UrlFetchOptions& fetchOptions
)
{
    if (sourceId.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "CubeCobra URL import requires a non-empty source id"
        );
    }

    const QString cubeId =
        cubeIdFromUrl(value, sourceId);
    const QUrl cubeUrl(
        value.trimmed(),
        QUrl::StrictMode
    );
    const QUrl apiUrl(
        QStringLiteral(
            "https://cubecobra.com/cube/api/cubeJSON/%1"
        ).arg(
            QString::fromLatin1(
                QUrl::toPercentEncoding(cubeId)
            )
        )
    );

    const UrlPayload payload =
        fetchUrlPayload(apiUrl, fetchOptions);
    const QJsonDocument document =
        parseJsonPayload(payload, sourceId);

    std::optional<QString> cubeName;
    const std::vector<NormalizedCard> cards =
        normalizeCube(
            document,
            sourceId,
            cubeName
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
            QStringLiteral("cubecobra-%1.json")
                .arg(cubeId),
        .order = order,
        .originalFormat = QStringLiteral("json"),
        .mediaType =
            QStringLiteral("application/json"),
        .sourceUrl = sanitizeUrlForReport(
            cubeUrl.toString(QUrl::FullyEncoded)
        ),
        .adapterId = QStringLiteral("cubecobra"),
        .sizeBytes =
            static_cast<std::uint64_t>(
                normalized.size()
            ),
        .originalBytes = normalized,
        .sha256 = normalizedSha,
        .metadata = QJsonObject{
            {
                QStringLiteral("adapterId"),
                QStringLiteral("cubecobra")
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
    if (cubeName.has_value()) {
        source.metadata.insert(
            QStringLiteral("cubeName"),
            *cubeName
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
                    + QStringLiteral(":cubecobra:")
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
                    .scryfallId = card.scryfallId,
                    .section = card.section,
                },
                .section = card.section,
                .metadata = QJsonObject{
                    {
                        QStringLiteral("adapterId"),
                        QStringLiteral("cubecobra")
                    },
                },
            }
        );
    }

    output.metadata = QJsonObject{
        {
            QStringLiteral("adapterId"),
            QStringLiteral("cubecobra")
        },
        {
            QStringLiteral("cubeId"),
            cubeId
        },
    };
    if (cubeName.has_value()) {
        output.metadata.insert(
            QStringLiteral("cubeName"),
            *cubeName
        );
    }

    return CubeCobraUrlImportResult{
        .source = std::move(source),
        .output = std::move(output),
    };
}

} // namespace tcgprint::imports
