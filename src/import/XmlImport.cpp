#include "import/XmlImport.h"

#include "import/ImportFailure.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <set>

namespace tcgprint::imports {
namespace {

QString decodeXmlBytes(const QByteArray& input)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(input);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral("XML input must be valid UTF-8."),
            QStringLiteral("INVALID_XML")
        );
    }
    return text;
}

void incrementNode(
    std::uint64_t depth,
    std::uint64_t& nodeCount,
    std::uint64_t& maxDepth,
    const ImportLimits& limits
)
{
    ++nodeCount;
    maxDepth = std::max(maxDepth, depth);
    if (nodeCount > limits.maxXmlNodes) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured node limit."),
            QStringLiteral("INVALID_XML")
        );
    }
    if (depth > limits.maxXmlDepth) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured depth limit."),
            QStringLiteral("INVALID_XML")
        );
    }
}

SafeXmlNode parseElement(
    QXmlStreamReader& reader,
    std::uint64_t depth,
    std::uint64_t& nodeCount,
    std::uint64_t& maxDepth,
    const ImportLimits& limits
)
{
    incrementNode(
        depth,
        nodeCount,
        maxDepth,
        limits
    );

    SafeXmlNode node;
    node.name = reader.name().toString();
    for (const QXmlStreamAttribute& attribute : reader.attributes()) {
        node.attributes.insert(
            attribute.name().toString(),
            attribute.value().toString()
        );
    }

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token =
            reader.readNext();

        switch (token) {
        case QXmlStreamReader::StartElement:
            node.children.push_back(
                parseElement(
                    reader,
                    depth + 1,
                    nodeCount,
                    maxDepth,
                    limits
                )
            );
            break;

        case QXmlStreamReader::Characters:
            if (!reader.text().isEmpty()) {
                incrementNode(
                    depth + 1,
                    nodeCount,
                    maxDepth,
                    limits
                );
                node.text += reader.text().toString();
            }
            break;

        case QXmlStreamReader::Comment:
        case QXmlStreamReader::ProcessingInstruction:
            incrementNode(
                depth + 1,
                nodeCount,
                maxDepth,
                limits
            );
            break;

        case QXmlStreamReader::DTD:
            throw ImportFailureError(
                QStringLiteral(
                    "XML DTD and entity declarations are blocked."
                ),
                QStringLiteral("XML_DTD_BLOCKED")
            );

        case QXmlStreamReader::EndElement:
            return node;

        case QXmlStreamReader::Invalid:
            throw ImportFailureError(
                QStringLiteral("XML is not well formed."),
                QStringLiteral("INVALID_XML")
            );

        default:
            break;
        }
    }

    throw ImportFailureError(
        QStringLiteral("XML is not well formed."),
        QStringLiteral("INVALID_XML")
    );
}

SafeXmlDocument parseSafeXmlText(
    QString text,
    std::uint64_t byteLength,
    const ImportLimitOverrides& limitOverrides
)
{
    const ImportLimits limits =
        resolveImportLimits(limitOverrides);
    if (byteLength > limits.maxXmlBytes) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured byte limit."),
            QStringLiteral("INPUT_TOO_LARGE")
        );
    }

    if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
        text.remove(0, 1);
    }

    const QRegularExpression blockedDeclaration(
        QStringLiteral(R"(<!s*(?:DOCTYPE|ENTITY))"),
        QRegularExpression::CaseInsensitiveOption
    );
    if (blockedDeclaration.match(text).hasMatch()) {
        throw ImportFailureError(
            QStringLiteral(
                "XML DTD and entity declarations are blocked."
            ),
            QStringLiteral("XML_DTD_BLOCKED")
        );
    }

    QXmlStreamReader reader(text);
    std::uint64_t nodeCount = 0;
    std::uint64_t maxDepth = 0;
    std::optional<SafeXmlNode> root;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token =
            reader.readNext();

        if (token == QXmlStreamReader::DTD) {
            throw ImportFailureError(
                QStringLiteral(
                    "XML DTD and entity declarations are blocked."
                ),
                QStringLiteral("XML_DTD_BLOCKED")
            );
        }
        if (token == QXmlStreamReader::StartElement) {
            if (root.has_value()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "XML contains multiple root elements."
                    ),
                    QStringLiteral("INVALID_XML")
                );
            }
            root = parseElement(
                reader,
                1,
                nodeCount,
                maxDepth,
                limits
            );
        }
        if (token == QXmlStreamReader::Invalid) {
            break;
        }
    }

    if (
        reader.hasError()
        || !root.has_value()
    ) {
        throw ImportFailureError(
            QStringLiteral("XML is not well formed."),
            QStringLiteral("INVALID_XML")
        );
    }

    return SafeXmlDocument{
        .root = std::move(*root),
        .nodeCount = nodeCount,
        .maxDepth = maxDepth,
    };
}


const SafeXmlNode* directChild(
    const SafeXmlNode& parent,
    const QString& name
)
{
    for (const SafeXmlNode& child : parent.children) {
        if (child.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &child;
        }
    }
    return nullptr;
}

std::vector<const SafeXmlNode*> directChildren(
    const SafeXmlNode& parent,
    const QString& name
)
{
    std::vector<const SafeXmlNode*> matches;
    for (const SafeXmlNode& child : parent.children) {
        if (child.name.compare(name, Qt::CaseInsensitive) == 0) {
            matches.push_back(&child);
        }
    }
    return matches;
}

std::optional<QString> childText(
    const SafeXmlNode& parent,
    const QString& name
)
{
    const SafeXmlNode* child = directChild(parent, name);
    if (child == nullptr) {
        return std::nullopt;
    }
    const QString value = child->text.trimmed();
    return value.isEmpty()
        ? std::nullopt
        : std::optional<QString>(value);
}

QJsonObject elementRecord(const SafeXmlNode& element)
{
    QJsonObject result;
    for (const SafeXmlNode& child : element.children) {
        const QString value = child.text.trimmed();
        if (!result.contains(child.name)) {
            result.insert(child.name, value);
        } else {
            result.insert(
                child.name,
                result.value(child.name).toString()
                    + QStringLiteral(",")
                    + value
            );
        }
    }
    return result;
}

QJsonObject importedAssetJson(const ImportedAsset& asset)
{
    QJsonObject value{
        {QStringLiteral("id"), asset.id},
        {QStringLiteral("sourceId"), asset.sourceId},
        {QStringLiteral("originalFormat"), asset.originalFormat},
    };
    if (asset.providerAssetId.has_value()) {
        value.insert(
            QStringLiteral("providerAssetId"),
            *asset.providerAssetId
        );
    }
    if (asset.selectedArtworkId.has_value()) {
        value.insert(
            QStringLiteral("selectedArtworkId"),
            *asset.selectedArtworkId
        );
    }
    value.insert(QStringLiteral("metadata"), asset.metadata);
    return value;
}

struct MpcCardRecord {
    ImportedFaceSide side{ImportedFaceSide::Front};
    int ordinal{0};
    QStringList slots;
    std::uint64_t quantity{1};
    std::optional<QString> providerId;
    std::optional<QString> name;
    std::optional<QString> query;
    QJsonObject rawFields;
    ImportedAsset asset;
    ImportedFace face;
};

QStringList splitSlots(const std::vector<const SafeXmlNode*>& values)
{
    QStringList result;
    const QRegularExpression separator(
        QStringLiteral(R"([\s,]+)")
    );
    for (const SafeXmlNode* node : values) {
        const QString text = node->text.trimmed();
        if (text.isEmpty()) {
            continue;
        }
        result.append(
            text.split(separator, Qt::SkipEmptyParts)
        );
    }
    return result;
}

QString sideName(ImportedFaceSide side)
{
    return side == ImportedFaceSide::Front
        ? QStringLiteral("front")
        : QStringLiteral("back");
}

std::vector<MpcCardRecord> parseMpcCardRecords(
    const SafeXmlNode* container,
    ImportedFaceSide side,
    const ImportSource& source,
    const std::optional<QString>& cardback,
    std::vector<ImportWarning>& warnings
)
{
    std::vector<MpcCardRecord> records;
    if (container == nullptr) {
        return records;
    }

    const auto cards =
        directChildren(*container, QStringLiteral("card"));
    records.reserve(cards.size());

    for (std::size_t index = 0; index < cards.size(); ++index) {
        const SafeXmlNode& card = *cards[index];
        const int ordinal = static_cast<int>(index + 1);

        const QStringList pluralSlots =
            splitSlots(
                directChildren(card, QStringLiteral("slots"))
            );
        const QStringList singularSlots =
            splitSlots(
                directChildren(card, QStringLiteral("slot"))
            );

        if (
            !pluralSlots.isEmpty()
            && !singularSlots.isEmpty()
            && pluralSlots != singularSlots
        ) {
            warnings.push_back(ImportWarning{
                .code = QStringLiteral("AMBIGUOUS_MPC_SLOTS"),
                .message = QStringLiteral(
                    "Conflicting <slots> and <slot> values; <slots> remains authoritative."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
            });
        }

        const QStringList slots =
            !pluralSlots.isEmpty()
            ? pluralSlots
            : singularSlots;

        const auto providerId =
            childText(card, QStringLiteral("id"));
        const auto name =
            childText(card, QStringLiteral("name"));
        const auto query =
            childText(card, QStringLiteral("query"));
        const auto quantityText =
            childText(card, QStringLiteral("quantity"));

        std::optional<std::uint64_t> explicitQuantity;
        if (quantityText.has_value()) {
            bool ok = false;
            const qulonglong parsed =
                quantityText->toULongLong(&ok);
            constexpr qulonglong MaxSafeInteger =
                9'007'199'254'740'991ULL;
            if (
                ok
                && parsed > 0
                && parsed <= MaxSafeInteger
            ) {
                explicitQuantity =
                    static_cast<std::uint64_t>(parsed);
            } else {
                warnings.push_back(ImportWarning{
                    .code = QStringLiteral("INVALID_MPC_QUANTITY"),
                    .message = QStringLiteral(
                        "Invalid MPC quantity; slots remain authoritative when present."
                    ),
                    .sourceId = source.id,
                    .sourceFilename = source.filename,
                    .sourcePath = source.sourcePath,
                });
            }
        }

        const std::uint64_t quantity =
            !slots.isEmpty()
            ? static_cast<std::uint64_t>(slots.size())
            : explicitQuantity.value_or(1);

        if (
            slots.isEmpty()
            && !explicitQuantity.has_value()
        ) {
            warnings.push_back(ImportWarning{
                .code = QStringLiteral("MPC_SLOTS_MISSING"),
                .message = QStringLiteral(
                    "MPC card has no slot or valid quantity; defaulted to one."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
            });
        } else if (
            explicitQuantity.has_value()
            && !slots.isEmpty()
            && *explicitQuantity
                != static_cast<std::uint64_t>(slots.size())
        ) {
            warnings.push_back(ImportWarning{
                .code = QStringLiteral(
                    "MPC_QUANTITY_DIFFERS_FROM_SLOTS"
                ),
                .message = QStringLiteral(
                    "MPC quantity differs from slots; slots remain authoritative."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
            });
        }

        const QString assetId =
            source.id
            + QStringLiteral(":mpc:")
            + sideName(side)
            + QStringLiteral(":")
            + QString::number(ordinal);

        QJsonObject assetMetadata{
            {
                QStringLiteral("rawFields"),
                elementRecord(card)
            },
        };
        if (cardback.has_value()) {
            assetMetadata.insert(
                QStringLiteral("cardback"),
                *cardback
            );
        }

        ImportedAsset asset{
            .id = assetId,
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
            .originalFormat =
                QStringLiteral("mpc-artwork-reference"),
            .providerAssetId = providerId,
            .selectedArtworkId = providerId,
            .metadata = std::move(assetMetadata),
        };

        ImportedFace face{
            .side = side,
            .asset = asset,
            .providerAssetId = providerId,
            .selectedArtworkId = providerId,
            .name = name,
            .query = query,
            .slots = slots,
            .metadata = QJsonObject{
                {
                    QStringLiteral("rawFields"),
                    elementRecord(card)
                },
            },
        };

        records.push_back(MpcCardRecord{
            .side = side,
            .ordinal = ordinal,
            .slots = slots,
            .quantity = quantity,
            .providerId = providerId,
            .name = name,
            .query = query,
            .rawFields = elementRecord(card),
            .asset = std::move(asset),
            .face = std::move(face),
        });
    }

    return records;
}

ImportedEntry entryForMpcCard(
    const ImportSource& source,
    const MpcCardRecord* front,
    const std::vector<const MpcCardRecord*>& backRecords,
    const std::optional<QString>& cardback,
    const std::optional<ImportedAsset>& cardbackAsset,
    const QJsonObject& details,
    std::vector<ImportWarning>& warnings,
    int order
)
{
    std::vector<const MpcCardRecord*> associatedBacks;
    std::vector<ImportedFaceAssociation> associations;

    if (front != nullptr) {
        for (const QString& slot : front->slots) {
            std::vector<const MpcCardRecord*> matches;
            for (const MpcCardRecord* back : backRecords) {
                if (back->slots.contains(slot)) {
                    matches.push_back(back);
                }
            }

            ImportedFaceAssociation association{
                .slot = slot,
                .frontAssetId = front->asset.id,
            };

            if (matches.size() == 1) {
                association.backAssetId =
                    matches.front()->asset.id;
                associatedBacks.push_back(matches.front());
            } else if (matches.size() > 1) {
                warnings.push_back(ImportWarning{
                    .code = QStringLiteral(
                        "AMBIGUOUS_MPC_BACK_PAIRING"
                    ),
                    .message = QStringLiteral(
                        "An MPC slot matches multiple back records; no association was guessed."
                    ),
                    .sourceId = source.id,
                    .sourceFilename = source.filename,
                    .sourcePath = source.sourcePath,
                });
            }
            associations.push_back(std::move(association));
        }
    } else {
        associatedBacks = backRecords;
    }

    std::vector<const MpcCardRecord*> uniqueBacks;
    std::set<QString> seenBackIds;
    for (const MpcCardRecord* back : associatedBacks) {
        if (seenBackIds.insert(back->asset.id).second) {
            uniqueBacks.push_back(back);
        }
    }

    const MpcCardRecord* primary =
        front != nullptr
        ? front
        : (
            backRecords.empty()
            ? nullptr
            : backRecords.front()
        );

    ImportedEntry entry{
        .id = source.id
            + QStringLiteral(":mpc-entry:")
            + QString::number(order),
        .kind = ImportedEntryKind::MpcOrderCard,
        .order = source.order + order,
        .quantity = primary != nullptr ? primary->quantity : 1,
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
    };

    if (front != nullptr) {
        entry.front = front->face;
        entry.slots = front->slots;
        entry.faces.push_back(front->face);
        if (front->name.has_value()) {
            entry.nameSuggestion = front->name;
            entry.cardHint = ImportedCardHint{
                .name = front->name,
            };
        }
    } else if (primary != nullptr) {
        entry.slots = primary->slots;
    }

    if (cardbackAsset.has_value()) {
        entry.cardbackAsset = cardbackAsset;
    }

    if (uniqueBacks.size() == 1) {
        entry.back = uniqueBacks.front()->face;
    }
    for (const MpcCardRecord* back : uniqueBacks) {
        entry.faces.push_back(back->face);
    }
    entry.faceAssociations = std::move(associations);

    entry.metadata.insert(
        QStringLiteral("parser"),
        QStringLiteral("mpc-autofill-xml")
    );
    if (front != nullptr) {
        entry.metadata.insert(
            QStringLiteral("originalFrontFields"),
            front->rawFields
        );
        entry.metadata.insert(
            QStringLiteral("originalFrontOrder"),
            front->ordinal
        );
    } else if (primary != nullptr) {
        entry.metadata.insert(
            QStringLiteral("originalBackFields"),
            primary->rawFields
        );
    }
    if (cardback.has_value()) {
        entry.metadata.insert(
            QStringLiteral("cardback"),
            *cardback
        );
    }
    if (cardbackAsset.has_value()) {
        entry.metadata.insert(
            QStringLiteral("cardbackAsset"),
            importedAssetJson(*cardbackAsset)
        );
    }
    entry.metadata.insert(
        QStringLiteral("details"),
        details
    );

    return entry;
}

} // namespace

SafeXmlDocument parseSafeXml(
    const QByteArray& input,
    const ImportLimitOverrides& limitOverrides
)
{
    return parseSafeXmlText(
        decodeXmlBytes(input),
        static_cast<std::uint64_t>(input.size()),
        limitOverrides
    );
}

SafeXmlDocument parseSafeXml(
    const QString& input,
    const ImportLimitOverrides& limitOverrides
)
{
    const QByteArray utf8 = input.toUtf8();
    return parseSafeXmlText(
        input,
        static_cast<std::uint64_t>(utf8.size()),
        limitOverrides
    );
}

ImporterOutput importGenericXml(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides
)
{
    if (!source.originalBytes.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "XML import requires original source bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    const SafeXmlDocument document =
        parseSafeXml(
            *source.originalBytes,
            limitOverrides
        );

    ImporterOutput output;
    output.entries.push_back(ImportedEntry{
        .id = source.id + QStringLiteral(":document"),
        .kind = ImportedEntryKind::Document,
        .order = source.order,
        .quantity = 1,
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
        .metadata = QJsonObject{
            {
                QStringLiteral("parser"),
                QStringLiteral("generic-xml")
            },
            {
                QStringLiteral("rootName"),
                document.root.name
            },
            {
                QStringLiteral("rootAttributes"),
                document.root.attributes
            },
        },
    });
    return output;
}


ImporterOutput importMpcAutofillXml(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides
)
{
    if (!source.originalBytes.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "MPC Autofill import requires original XML bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    const SafeXmlDocument document =
        parseSafeXml(
            *source.originalBytes,
            limitOverrides
        );
    if (
        document.root.name.compare(
            QStringLiteral("order"),
            Qt::CaseInsensitive
        ) != 0
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Selected MPC Autofill XML must have an <order> root."
            ),
            QStringLiteral("FORMAT_MISMATCH"),
            source.id,
            source.sourcePath
        );
    }

    ImporterOutput output;
    const SafeXmlNode& root = document.root;

    QJsonObject details;
    if (
        const SafeXmlNode* detailsNode =
            directChild(root, QStringLiteral("details"))
    ) {
        details = elementRecord(*detailsNode);
    }

    const auto cardback =
        childText(root, QStringLiteral("cardback"));

    std::optional<ImportedAsset> cardbackAsset;
    if (cardback.has_value()) {
        cardbackAsset = ImportedAsset{
            .id = source.id
                + QStringLiteral(":mpc:cardback"),
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
            .originalFormat =
                QStringLiteral("mpc-cardback-reference"),
            .providerAssetId = cardback,
            .selectedArtworkId = cardback,
            .metadata = QJsonObject{
                {
                    QStringLiteral("rawValue"),
                    *cardback
                },
            },
        };
    }

    std::vector<MpcCardRecord> fronts =
        parseMpcCardRecords(
            directChild(root, QStringLiteral("fronts")),
            ImportedFaceSide::Front,
            source,
            cardback,
            output.warnings
        );
    std::vector<MpcCardRecord> backs =
        parseMpcCardRecords(
            directChild(root, QStringLiteral("backs")),
            ImportedFaceSide::Back,
            source,
            cardback,
            output.warnings
        );

    std::map<QString, int> frontSlotCounts;
    for (const MpcCardRecord& front : fronts) {
        for (const QString& slot : front.slots) {
            ++frontSlotCounts[slot];
        }
    }

    for (
        std::size_t index = 0;
        index < fronts.size();
        ++index
    ) {
        const MpcCardRecord& front = fronts[index];
        QSet<QString> ambiguousFrontSlots;
        for (const QString& slot : front.slots) {
            if (frontSlotCounts[slot] != 1) {
                ambiguousFrontSlots.insert(slot);
                output.warnings.push_back(ImportWarning{
                    .code = QStringLiteral(
                        "AMBIGUOUS_MPC_FRONT_SLOT"
                    ),
                    .message = QStringLiteral(
                        "MPC slot is reused by multiple front records; no back association was guessed."
                    ),
                    .sourceId = source.id,
                    .sourceFilename = source.filename,
                    .sourcePath = source.sourcePath,
                });
            }
        }

        std::vector<const MpcCardRecord*> candidateBacks;
        for (const MpcCardRecord& back : backs) {
            bool containsAmbiguous = false;
            for (const QString& slot : back.slots) {
                if (ambiguousFrontSlots.contains(slot)) {
                    containsAmbiguous = true;
                    break;
                }
            }
            if (!containsAmbiguous) {
                candidateBacks.push_back(&back);
            }
        }

        output.entries.push_back(
            entryForMpcCard(
                source,
                &front,
                candidateBacks,
                cardback,
                cardbackAsset,
                details,
                output.warnings,
                static_cast<int>(index)
            )
        );
    }

    QSet<QString> pairedBackIds;
    for (const ImportedEntry& entry : output.entries) {
        for (
            const ImportedFaceAssociation& association :
            entry.faceAssociations
        ) {
            if (association.backAssetId.has_value()) {
                pairedBackIds.insert(
                    *association.backAssetId
                );
            }
        }
    }

    for (
        std::size_t index = 0;
        index < backs.size();
        ++index
    ) {
        const MpcCardRecord& back = backs[index];
        if (pairedBackIds.contains(back.asset.id)) {
            continue;
        }
        output.entries.push_back(
            entryForMpcCard(
                source,
                nullptr,
                std::vector<const MpcCardRecord*>{&back},
                cardback,
                cardbackAsset,
                details,
                output.warnings,
                static_cast<int>(fronts.size() + index)
            )
        );
    }

    if (output.entries.empty()) {
        ImportedEntry documentEntry{
            .id = source.id + QStringLiteral(":document"),
            .kind = ImportedEntryKind::Document,
            .order = source.order,
            .quantity = 1,
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
            .cardbackAsset = cardbackAsset,
            .metadata = QJsonObject{
                {
                    QStringLiteral("parser"),
                    QStringLiteral("mpc-autofill-xml")
                },
                {
                    QStringLiteral("details"),
                    details
                },
            },
        };
        if (cardback.has_value()) {
            documentEntry.metadata.insert(
                QStringLiteral("cardback"),
                *cardback
            );
        }
        if (cardbackAsset.has_value()) {
            documentEntry.metadata.insert(
                QStringLiteral("cardbackAsset"),
                importedAssetJson(*cardbackAsset)
            );
        }
        output.entries.push_back(
            std::move(documentEntry)
        );
    }

    output.metadata.insert(
        QStringLiteral("details"),
        details
    );
    if (cardback.has_value()) {
        output.metadata.insert(
            QStringLiteral("cardback"),
            *cardback
        );
    }
    if (cardbackAsset.has_value()) {
        output.metadata.insert(
            QStringLiteral("cardbackAsset"),
            importedAssetJson(*cardbackAsset)
        );
    }

    return output;
}

} // namespace tcgprint::imports
