#include "import/JsonImport.h"

#include "import/ImportFailure.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QStringDecoder>

#include <array>

namespace tcgprint::imports {
namespace {

struct Collection {
    QString path;
    std::vector<QJsonValue> rows;
};

QString decodeText(const ImportSource& source)
{
    if (source.originalText.has_value()) {
        return *source.originalText;
    }
    if (!source.originalBytes.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "JSON import requires original text or bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(*source.originalBytes);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral("JSON must be valid UTF-8."),
            QStringLiteral("INVALID_JSON"),
            source.id,
            source.sourcePath
        );
    }
    return text;
}

QString normalizedKey(QString key)
{
    key = key.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    return key;
}

const std::array<QStringList, 8>& aliases()
{
    static const std::array<QStringList, 8> values{
        QStringList{
            QStringLiteral("name"),
            QStringLiteral("cardname")
        },
        QStringList{
            QStringLiteral("quantity"),
            QStringLiteral("count"),
            QStringLiteral("qty"),
            QStringLiteral("copies")
        },
        QStringList{
            QStringLiteral("set"),
            QStringLiteral("setcode")
        },
        QStringList{
            QStringLiteral("collectornumber"),
            QStringLiteral("collector")
        },
        QStringList{QStringLiteral("scryfallid")},
        QStringList{
            QStringLiteral("imageurl"),
            QStringLiteral("image"),
            QStringLiteral("imageuri")
        },
        QStringList{
            QStringLiteral("language"),
            QStringLiteral("lang")
        },
        QStringList{
            QStringLiteral("section"),
            QStringLiteral("board"),
            QStringLiteral("zone")
        },
    };
    return values;
}

QJsonValue rootValue(const QJsonDocument& document)
{
    if (document.isArray()) {
        return document.array();
    }
    return document.object();
}

void checkBounds(
    const QJsonValue& root,
    const ImportLimits& limits,
    const ImportSource& source
)
{
    struct Pending {
        QJsonValue value;
        std::uint64_t depth;
    };

    std::vector<Pending> pending{
        Pending{root, 1}
    };
    std::uint64_t nodeCount = 0;

    while (!pending.empty()) {
        Pending current = std::move(pending.back());
        pending.pop_back();
        ++nodeCount;

        if (nodeCount > limits.maxJsonNodes) {
            throw ImportFailureError(
                QStringLiteral(
                    "JSON exceeds the configured node limit."
                ),
                QStringLiteral("INVALID_JSON"),
                source.id,
                source.sourcePath
            );
        }
        if (current.depth > limits.maxJsonDepth) {
            throw ImportFailureError(
                QStringLiteral(
                    "JSON exceeds the configured depth limit."
                ),
                QStringLiteral("INVALID_JSON"),
                source.id,
                source.sourcePath
            );
        }

        if (current.value.isArray()) {
            const QJsonArray array = current.value.toArray();
            for (const QJsonValue& child : array) {
                pending.push_back(
                    Pending{child, current.depth + 1}
                );
            }
        } else if (current.value.isObject()) {
            const QJsonObject object = current.value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                pending.push_back(
                    Pending{it.value(), current.depth + 1}
                );
            }
        }
    }
}

std::vector<QJsonValue> pathValues(
    const QJsonValue& root,
    const QString& path
)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }

    std::vector<QJsonValue> current{root};
    const QStringList parts =
        path.split(QLatin1Char('.'), Qt::KeepEmptyParts);

    for (const QString& rawPart : parts) {
        if (rawPart.isEmpty()) {
            return {};
        }

        const bool expands =
            rawPart.endsWith(QStringLiteral("[]"));
        const QString key =
            expands
            ? rawPart.left(rawPart.size() - 2)
            : rawPart;

        std::vector<QJsonValue> next;
        for (const QJsonValue& value : current) {
            if (key.isEmpty()) {
                if (expands && value.isArray()) {
                    for (
                        const QJsonValue& child :
                        value.toArray()
                    ) {
                        next.push_back(child);
                    }
                }
                continue;
            }

            if (!value.isObject()) {
                continue;
            }
            const QJsonObject object = value.toObject();
            if (!object.contains(key)) {
                continue;
            }

            const QJsonValue child = object.value(key);
            if (expands) {
                if (child.isArray()) {
                    for (
                        const QJsonValue& item :
                        child.toArray()
                    ) {
                        next.push_back(item);
                    }
                }
            } else {
                next.push_back(child);
            }
        }
        current = std::move(next);
    }

    return current;
}

std::optional<QString> findAlias(
    const QJsonObject& record,
    const QStringList& fieldAliases
)
{
    for (auto it = record.begin(); it != record.end(); ++it) {
        if (fieldAliases.contains(normalizedKey(it.key()))) {
            return it.key();
        }
    }
    return std::nullopt;
}

std::optional<QString> usableText(const QJsonValue& value)
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
            return QString::number(number, 'g', 17);
        }
    }
    return std::nullopt;
}

std::optional<Collection> inferredCollection(
    const QJsonValue& root
)
{
    if (root.isArray()) {
        std::vector<QJsonValue> rows;
        for (const QJsonValue& value : root.toArray()) {
            rows.push_back(value);
        }
        return Collection{
            .path = QStringLiteral("[]"),
            .rows = std::move(rows),
        };
    }

    if (!root.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = root.toObject();
    for (
        const QString& key :
        {
            QStringLiteral("cards"),
            QStringLiteral("deck"),
            QStringLiteral("items"),
            QStringLiteral("entries")
        }
    ) {
        const QJsonValue value = object.value(key);
        if (value.isArray()) {
            std::vector<QJsonValue> rows;
            for (const QJsonValue& row : value.toArray()) {
                rows.push_back(row);
            }
            return Collection{
                .path = key + QStringLiteral("[]"),
                .rows = std::move(rows),
            };
        }
    }

    return std::nullopt;
}

QString pathInsideCollection(
    const QString& path,
    const QString& collectionPath
)
{
    const QString prefix =
        collectionPath.endsWith(QStringLiteral("[]"))
        ? collectionPath
        : collectionPath + QStringLiteral("[]");

    if (path.startsWith(prefix + QLatin1Char('.'))) {
        return path.mid(prefix.size() + 1);
    }
    if (path == prefix) {
        return {};
    }
    return path;
}

std::optional<QString> mappedPath(
    const JsonImportMapping& mapping,
    int index
)
{
    switch (index) {
    case 0: return mapping.name;
    case 1: return mapping.quantity;
    case 2: return mapping.setCode;
    case 3: return mapping.collectorNumber;
    case 4: return mapping.scryfallId;
    case 5: return mapping.imageUrl;
    case 6: return mapping.language;
    case 7: return mapping.section;
    default: return std::nullopt;
    }
}

std::optional<QJsonValue> valueFor(
    const QJsonObject& row,
    int field,
    const std::array<std::optional<QString>, 8>& explicitPaths
)
{
    if (explicitPaths[field].has_value()) {
        const QString& path = *explicitPaths[field];
        if (path.isEmpty()) {
            return QJsonValue(row);
        }
        const auto values =
            pathValues(QJsonValue(row), path);
        if (!values.empty()) {
            return values.front();
        }
        return std::nullopt;
    }

    const auto key = findAlias(row, aliases()[field]);
    if (!key.has_value()) {
        return std::nullopt;
    }
    return row.value(*key);
}

} // namespace

ImporterOutput parseJsonImport(
    const ImportSource& source,
    const std::optional<JsonImportMapping>& mapping,
    const ImportLimitOverrides& limitOverrides
)
{
    const ImportLimits limits =
        resolveImportLimits(limitOverrides);
    QString text = decodeText(source);
    if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
        text.remove(0, 1);
    }

    const qsizetype byteLength = text.toUtf8().size();
    if (
        byteLength < 0
        || static_cast<std::uint64_t>(byteLength)
            > limits.maxJsonBytes
    ) {
        throw ImportFailureError(
            QStringLiteral("JSON exceeds the configured byte limit."),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            text.toUtf8(),
            &parseError
        );
    if (
        document.isNull()
        || parseError.error != QJsonParseError::NoError
    ) {
        throw ImportFailureError(
            QStringLiteral("JSON is malformed."),
            QStringLiteral("INVALID_JSON"),
            source.id,
            source.sourcePath
        );
    }

    const QJsonValue root = rootValue(document);
    checkBounds(root, limits, source);

    std::optional<QString> requestedCollectionPath;
    if (
        mapping.has_value()
        && mapping->collectionPath.has_value()
    ) {
        requestedCollectionPath = mapping->collectionPath;
    } else if (mapping.has_value()) {
        for (int index = 0; index < 8; ++index) {
            const auto path = mappedPath(*mapping, index);
            if (
                path.has_value()
                && path->contains(QStringLiteral("[]"))
            ) {
                requestedCollectionPath =
                    path->section(QStringLiteral("[]"), 0, 0)
                    + QStringLiteral("[]");
                break;
            }
        }
    }

    std::optional<Collection> collection;
    if (requestedCollectionPath.has_value()) {
        QString path = *requestedCollectionPath;
        if (
            path != QStringLiteral("[]")
            && !path.endsWith(QStringLiteral("[]"))
        ) {
            path += QStringLiteral("[]");
        }
        collection = Collection{
            .path = path,
            .rows = pathValues(root, path),
        };
    } else {
        collection = inferredCollection(root);
    }

    ImporterOutput output;
    if (!collection.has_value()) {
        output.warnings.push_back(ImportWarning{
            .code = QStringLiteral("NO_CARD_COLLECTION"),
            .message = QStringLiteral(
                "JSON structure has no recognized card array."
            ),
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
        });
        output.mappings.push_back(ImportMapping{
            .sourceId = source.id,
            .format = QStringLiteral("json"),
            .fields = {},
            .unknownFields = {},
        });
        return output;
    }

    if (
        static_cast<std::uint64_t>(collection->rows.size())
        > limits.maxCsvRows
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "JSON collection exceeds the configured row limit."
            ),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    const JsonImportMapping emptyMapping;
    const JsonImportMapping& config =
        mapping.has_value() ? *mapping : emptyMapping;

    std::array<std::optional<QString>, 8> explicitPaths;
    QJsonObject mappingFields;
    static const std::array<const char*, 8> fieldNames{
        "name",
        "quantity",
        "setCode",
        "collectorNumber",
        "scryfallId",
        "imageUrl",
        "language",
        "section",
    };

    for (int index = 0; index < 8; ++index) {
        const auto configured = mappedPath(config, index);
        if (configured.has_value()) {
            explicitPaths[index] =
                pathInsideCollection(
                    *configured,
                    collection->path
                );
            mappingFields.insert(
                QString::fromLatin1(fieldNames[index]),
                *configured
            );
        }
    }

    QSet<QString> knownAliases;
    for (const QStringList& group : aliases()) {
        for (const QString& alias : group) {
            knownAliases.insert(alias);
        }
    }
    QSet<QString> unknownFields;

    for (
        std::size_t index = 0;
        index < collection->rows.size();
        ++index
    ) {
        const QJsonValue& rowValue =
            collection->rows[index];
        if (!rowValue.isObject()) {
            output.warnings.push_back(ImportWarning{
                .code = QStringLiteral("INVALID_CARD_ROW"),
                .message = QStringLiteral(
                    "JSON card collection item is not an object."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
                .line = static_cast<int>(index + 1),
            });
            continue;
        }

        const QJsonObject row = rowValue.toObject();
        for (auto it = row.begin(); it != row.end(); ++it) {
            if (!knownAliases.contains(normalizedKey(it.key()))) {
                unknownFields.insert(it.key());
            }
        }

        const auto valueText = [&](int field)
            -> std::optional<QString> {
            const auto value =
                valueFor(row, field, explicitPaths);
            return value.has_value()
                ? usableText(*value)
                : std::nullopt;
        };

        const auto name = valueText(0);
        if (!name.has_value()) {
            output.warnings.push_back(ImportWarning{
                .code = QStringLiteral("MISSING_NAME"),
                .message = QStringLiteral(
                    "JSON card row has no mapped card name."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
                .line = static_cast<int>(index + 1),
            });
            continue;
        }

        const auto rawQuantity = valueText(1);
        std::uint64_t quantity = 1;
        if (rawQuantity.has_value()) {
            bool ok = false;
            quantity = rawQuantity->toULongLong(&ok);
            constexpr std::uint64_t MaxSafeInteger =
                9'007'199'254'740'991ULL;
            if (
                !ok
                || quantity == 0
                || quantity > MaxSafeInteger
            ) {
                output.warnings.push_back(ImportWarning{
                    .code = QStringLiteral("INVALID_QUANTITY"),
                    .message = QStringLiteral(
                        "Quantity is not a positive safe integer."
                    ),
                    .sourceId = source.id,
                    .sourceFilename = source.filename,
                    .sourcePath = source.sourcePath,
                    .line = static_cast<int>(index + 1),
                    .field = QStringLiteral("quantity"),
                });
                continue;
            }
        }

        const auto section = valueText(7);
        ImportedCardHint hint{
            .name = name,
            .setCode = valueText(2),
            .collectorNumber = valueText(3),
            .scryfallId = valueText(4),
            .imageUrl = valueText(5),
            .language = valueText(6),
            .section = section,
        };

        output.entries.push_back(ImportedEntry{
            .id = QStringLiteral("%1:json:%2")
                .arg(
                    source.id,
                    QString::number(index + 1)
                ),
            .kind = ImportedEntryKind::DeckCard,
            .order =
                source.order
                + static_cast<int>(output.entries.size()),
            .quantity = quantity,
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
            .cardHint = std::move(hint),
            .section = section,
            .metadata = QJsonObject{
                {
                    QStringLiteral("rawRecord"),
                    row
                },
                {
                    QStringLiteral("row"),
                    static_cast<int>(index + 1)
                },
            },
        });
    }

    QStringList unknown = unknownFields.values();
    unknown.sort();

    output.mappings.push_back(ImportMapping{
        .sourceId = source.id,
        .format = QStringLiteral("json"),
        .fields = mappingFields,
        .unknownFields = unknown,
    });

    return output;
}

} // namespace tcgprint::imports
