#include "persistence/projects/WorkingCardCodec.h"

#include "persistence/projects/ProjectSnapshot.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tcgprint::projects {
namespace {

[[noreturn]] void invalid(const std::string& message)
{
    throw ProjectSnapshotError(
        ProjectSnapshotErrorCode::InvalidProjectSnapshot,
        message
    );
}

std::string stringValue(
    const QJsonObject& object,
    const char* key
)
{
    return object.value(QLatin1String(key)).toString().toStdString();
}

std::optional<std::string> optionalStringValue(
    const QJsonObject& object,
    const char* key
)
{
    if (!object.contains(QLatin1String(key))) {
        return std::nullopt;
    }

    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        return std::nullopt;
    }

    return value.toString().toStdString();
}

void insertOptionalString(
    QJsonObject& object,
    const char* key,
    const std::optional<std::string>& value
)
{
    if (value) {
        object.insert(
            QLatin1String(key),
            QString::fromStdString(*value)
        );
    }
}

cards::CardFaceSide faceSide(const QString& value)
{
    if (value == QStringLiteral("front")) {
        return cards::CardFaceSide::Front;
    }
    if (value == QStringLiteral("back")) {
        return cards::CardFaceSide::Back;
    }
    invalid("WorkingCard contains unsupported face side.");
}

QString faceSideString(cards::CardFaceSide value)
{
    return value == cards::CardFaceSide::Back
        ? QStringLiteral("back")
        : QStringLiteral("front");
}

cards::ArtworkSource artworkSource(const QString& value)
{
    if (value == QStringLiteral("scryfall")) {
        return cards::ArtworkSource::Scryfall;
    }
    if (value == QStringLiteral("upload")) {
        return cards::ArtworkSource::Upload;
    }
    if (value == QStringLiteral("mpc")) {
        return cards::ArtworkSource::Mpc;
    }
    if (value == QStringLiteral("url")) {
        return cards::ArtworkSource::Url;
    }
    if (value == QStringLiteral("custom")) {
        return cards::ArtworkSource::Custom;
    }
    invalid("WorkingCard contains unsupported artwork source.");
}

QString artworkSourceString(cards::ArtworkSource value)
{
    switch (value) {
    case cards::ArtworkSource::Scryfall:
        return QStringLiteral("scryfall");
    case cards::ArtworkSource::Upload:
        return QStringLiteral("upload");
    case cards::ArtworkSource::Mpc:
        return QStringLiteral("mpc");
    case cards::ArtworkSource::Url:
        return QStringLiteral("url");
    case cards::ArtworkSource::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

cards::IdentityResolutionMethod identityMethod(
    const QString& value
)
{
    if (value == QStringLiteral("scryfall-id")) {
        return cards::IdentityResolutionMethod::ScryfallId;
    }
    if (value == QStringLiteral("set-collector")) {
        return cards::IdentityResolutionMethod::SetCollector;
    }
    if (value == QStringLiteral("name")) {
        return cards::IdentityResolutionMethod::Name;
    }
    if (value == QStringLiteral("filename")) {
        return cards::IdentityResolutionMethod::Filename;
    }
    if (value == QStringLiteral("ocr")) {
        return cards::IdentityResolutionMethod::Ocr;
    }
    if (value == QStringLiteral("fuzzy")) {
        return cards::IdentityResolutionMethod::Fuzzy;
    }
    if (value == QStringLiteral("manual")) {
        return cards::IdentityResolutionMethod::Manual;
    }
    if (value == QStringLiteral("custom")) {
        return cards::IdentityResolutionMethod::Custom;
    }
    invalid("WorkingCard contains unsupported identity method.");
}

QString identityMethodString(cards::IdentityResolutionMethod value)
{
    switch (value) {
    case cards::IdentityResolutionMethod::ScryfallId:
        return QStringLiteral("scryfall-id");
    case cards::IdentityResolutionMethod::SetCollector:
        return QStringLiteral("set-collector");
    case cards::IdentityResolutionMethod::Name:
        return QStringLiteral("name");
    case cards::IdentityResolutionMethod::Filename:
        return QStringLiteral("filename");
    case cards::IdentityResolutionMethod::Ocr:
        return QStringLiteral("ocr");
    case cards::IdentityResolutionMethod::Fuzzy:
        return QStringLiteral("fuzzy");
    case cards::IdentityResolutionMethod::Manual:
        return QStringLiteral("manual");
    case cards::IdentityResolutionMethod::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("manual");
}

cards::IdentityResolutionStatus identityStatus(
    const QString& value
)
{
    if (value == QStringLiteral("resolved")) {
        return cards::IdentityResolutionStatus::Resolved;
    }
    if (value == QStringLiteral("suggested")) {
        return cards::IdentityResolutionStatus::Suggested;
    }
    if (value == QStringLiteral("ambiguous")) {
        return cards::IdentityResolutionStatus::Ambiguous;
    }
    if (value == QStringLiteral("unresolved")) {
        return cards::IdentityResolutionStatus::Unresolved;
    }
    if (value == QStringLiteral("custom")) {
        return cards::IdentityResolutionStatus::Custom;
    }
    invalid("WorkingCard contains unsupported identity status.");
}

QString identityStatusString(cards::IdentityResolutionStatus value)
{
    switch (value) {
    case cards::IdentityResolutionStatus::Resolved:
        return QStringLiteral("resolved");
    case cards::IdentityResolutionStatus::Suggested:
        return QStringLiteral("suggested");
    case cards::IdentityResolutionStatus::Ambiguous:
        return QStringLiteral("ambiguous");
    case cards::IdentityResolutionStatus::Unresolved:
        return QStringLiteral("unresolved");
    case cards::IdentityResolutionStatus::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("unresolved");
}

cards::BackMode backMode(const QString& value)
{
    if (value == QStringLiteral("auto")) {
        return cards::BackMode::Auto;
    }
    if (value == QStringLiteral("project-default")) {
        return cards::BackMode::ProjectDefault;
    }
    if (value == QStringLiteral("manual")) {
        return cards::BackMode::Manual;
    }
    if (value == QStringLiteral("none")) {
        return cards::BackMode::None;
    }
    invalid("WorkingCard contains unsupported back mode.");
}

QString backModeString(cards::BackMode value)
{
    switch (value) {
    case cards::BackMode::Auto:
        return QStringLiteral("auto");
    case cards::BackMode::ProjectDefault:
        return QStringLiteral("project-default");
    case cards::BackMode::Manual:
        return QStringLiteral("manual");
    case cards::BackMode::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("project-default");
}

cards::BackModeSelectionPolicy backPolicy(
    const QString& value
)
{
    if (value == QStringLiteral("automatic")) {
        return cards::BackModeSelectionPolicy::Automatic;
    }
    if (value == QStringLiteral("explicit")) {
        return cards::BackModeSelectionPolicy::Explicit;
    }
    invalid("WorkingCard contains unsupported back selection policy.");
}

QString backPolicyString(cards::BackModeSelectionPolicy value)
{
    return value == cards::BackModeSelectionPolicy::Explicit
        ? QStringLiteral("explicit")
        : QStringLiteral("automatic");
}

std::vector<std::string> stringArray(const QJsonValue& value)
{
    std::vector<std::string> result;
    const QJsonArray array = value.toArray();
    result.reserve(static_cast<std::size_t>(array.size()));

    for (const QJsonValue& item : array) {
        result.push_back(item.toString().toStdString());
    }

    return result;
}

QJsonArray stringArrayJson(const std::vector<std::string>& values)
{
    QJsonArray result;
    for (const std::string& value : values) {
        result.append(QString::fromStdString(value));
    }
    return result;
}

cards::CardIdentityMetadata parseIdentityMetadata(
    const QJsonObject& metadata
)
{
    cards::CardIdentityMetadata result;

    for (const QString& key : {
        QStringLiteral("layout"),
        QStringLiteral("digital"),
        QStringLiteral("promo"),
        QStringLiteral("fullArt"),
        QStringLiteral("imageStatus"),
    }) {
        if (!metadata.contains(key)) {
            continue;
        }

        const QJsonValue value = metadata.value(key);
        if (value.isBool()) {
            result.scalars.emplace(
                key.toStdString(),
                value.toBool()
            );
        } else {
            result.scalars.emplace(
                key.toStdString(),
                value.toString().toStdString()
            );
        }
    }

    for (const QJsonValue& value :
         metadata.value(QStringLiteral("faces")).toArray()) {
        const QJsonObject face = value.toObject();
        result.faces.push_back(cards::IdentityMetadataFace{
            .name = stringValue(face, "name"),
        });
    }

    for (const QJsonValue& value :
         metadata.value(QStringLiteral("relatedCards")).toArray()) {
        const QJsonObject item = value.toObject();
        result.relatedCards.push_back(
            cards::IdentityMetadataRelatedCard{
                .id = stringValue(item, "id"),
                .name = stringValue(item, "name"),
                .component = stringValue(item, "component"),
                .typeLine = optionalStringValue(item, "typeLine"),
            }
        );
    }

    return result;
}

QJsonObject identityMetadataJson(
    const cards::CardIdentityMetadata& metadata
)
{
    QJsonObject result;

    for (const auto& [key, value] : metadata.scalars) {
        std::visit(
            [&](const auto& scalar) {
                using Scalar = std::decay_t<decltype(scalar)>;
                if constexpr (std::is_same_v<Scalar, std::string>) {
                    result.insert(
                        QString::fromStdString(key),
                        QString::fromStdString(scalar)
                    );
                } else {
                    result.insert(
                        QString::fromStdString(key),
                        scalar
                    );
                }
            },
            value
        );
    }

    if (!metadata.faces.empty()) {
        QJsonArray faces;
        for (const cards::IdentityMetadataFace& face :
             metadata.faces) {
            QJsonObject item;
            item.insert(
                QStringLiteral("name"),
                QString::fromStdString(face.name)
            );
            faces.append(item);
        }
        result.insert(QStringLiteral("faces"), faces);
    }

    if (!metadata.relatedCards.empty()) {
        QJsonArray related;
        for (const cards::IdentityMetadataRelatedCard& item :
             metadata.relatedCards) {
            QJsonObject object;
            object.insert(
                QStringLiteral("id"),
                QString::fromStdString(item.id)
            );
            object.insert(
                QStringLiteral("name"),
                QString::fromStdString(item.name)
            );
            object.insert(
                QStringLiteral("component"),
                QString::fromStdString(item.component)
            );
            insertOptionalString(
                object,
                "typeLine",
                item.typeLine
            );
            related.append(object);
        }
        result.insert(QStringLiteral("relatedCards"), related);
    }

    return result;
}

cards::CardIdentity parseIdentity(const QJsonObject& object)
{
    cards::CardIdentity result{
        .provider = stringValue(object, "provider"),
        .id = stringValue(object, "id"),
        .name = stringValue(object, "name"),
        .scryfallId = optionalStringValue(object, "scryfallId"),
        .oracleId = optionalStringValue(object, "oracleId"),
        .setCode = optionalStringValue(object, "setCode"),
        .collectorNumber = optionalStringValue(object, "collectorNumber"),
        .lang = optionalStringValue(object, "lang"),
        .resolutionMethod = identityMethod(
            object.value(QStringLiteral("resolutionMethod")).toString()
        ),
        .confidence =
            object.value(QStringLiteral("confidence")).toDouble(),
    };

    if (object.value(QStringLiteral("metadata")).isObject()) {
        result.metadata = parseIdentityMetadata(
            object.value(QStringLiteral("metadata")).toObject()
        );
    }

    return result;
}

QJsonObject identityJson(const cards::CardIdentity& identity)
{
    QJsonObject result;
    result.insert(
        QStringLiteral("id"),
        QString::fromStdString(identity.id)
    );
    result.insert(
        QStringLiteral("provider"),
        QString::fromStdString(identity.provider)
    );
    result.insert(
        QStringLiteral("name"),
        QString::fromStdString(identity.name)
    );
    insertOptionalString(result, "scryfallId", identity.scryfallId);
    insertOptionalString(result, "oracleId", identity.oracleId);
    insertOptionalString(result, "setCode", identity.setCode);
    insertOptionalString(
        result,
        "collectorNumber",
        identity.collectorNumber
    );
    insertOptionalString(result, "lang", identity.lang);
    result.insert(
        QStringLiteral("resolutionMethod"),
        identityMethodString(identity.resolutionMethod)
    );
    result.insert(
        QStringLiteral("confidence"),
        identity.confidence
    );

    if (identity.metadata) {
        result.insert(
            QStringLiteral("metadata"),
            identityMetadataJson(*identity.metadata)
        );
    }

    return result;
}

cards::SelectedArtwork parseSelectedArtwork(
    const QJsonObject& object
)
{
    cards::SelectedArtwork result{
        .candidateId = stringValue(object, "candidateId"),
        .source = artworkSource(
            object.value(QStringLiteral("source")).toString()
        ),
        .faceId = faceSide(
            object.value(QStringLiteral("faceId")).toString()
        ),
        .providerAssetId =
            optionalStringValue(object, "providerAssetId"),
        .selectedArtworkId =
            optionalStringValue(object, "selectedArtworkId"),
        .selectionPolicy =
            optionalStringValue(object, "selectionPolicy"),
    };

    const QJsonValue identityId =
        object.value(QStringLiteral("identityId"));
    if (identityId.isString()) {
        result.identityId =
            identityId.toString().toStdString();
    }

    return result;
}

QJsonObject selectedArtworkJson(
    const cards::SelectedArtwork& artwork
)
{
    QJsonObject result;
    result.insert(
        QStringLiteral("candidateId"),
        QString::fromStdString(artwork.candidateId)
    );
    result.insert(
        QStringLiteral("source"),
        artworkSourceString(artwork.source)
    );
    result.insert(
        QStringLiteral("identityId"),
        artwork.identityId
            ? QJsonValue(QString::fromStdString(*artwork.identityId))
            : QJsonValue(QJsonValue::Null)
    );
    result.insert(
        QStringLiteral("faceId"),
        faceSideString(artwork.faceId)
    );
    insertOptionalString(
        result,
        "providerAssetId",
        artwork.providerAssetId
    );
    insertOptionalString(
        result,
        "selectedArtworkId",
        artwork.selectedArtworkId
    );
    insertOptionalString(
        result,
        "selectionPolicy",
        artwork.selectionPolicy
    );
    return result;
}

cards::IdentityResolution parseIdentityResolution(
    const QJsonObject& object
)
{
    cards::IdentityResolution result{
        .status = identityStatus(
            object.value(QStringLiteral("status")).toString()
        ),
        .query = optionalStringValue(object, "query"),
        .confirmed =
            object.value(QStringLiteral("confirmed")).toBool(),
    };

    if (object.contains(QStringLiteral("method"))) {
        result.method = identityMethod(
            object.value(QStringLiteral("method")).toString()
        );
    }

    if (object.contains(QStringLiteral("confidence"))) {
        result.confidence =
            object.value(QStringLiteral("confidence")).toDouble();
    }

    const QJsonArray candidates =
        object.value(QStringLiteral("candidates")).toArray();
    result.candidates.reserve(
        static_cast<std::size_t>(candidates.size())
    );

    for (const QJsonValue& value : candidates) {
        const QJsonObject candidate = value.toObject();
        result.candidates.push_back(
            cards::IdentityResolutionCandidate{
                .identity = parseIdentity(
                    candidate
                        .value(QStringLiteral("identity"))
                        .toObject()
                ),
                .score =
                    candidate.value(QStringLiteral("score")).toDouble(),
                .reason = stringValue(candidate, "reason"),
            }
        );
    }

    return result;
}

QJsonObject identityResolutionJson(
    const cards::IdentityResolution& resolution
)
{
    QJsonObject result;
    result.insert(
        QStringLiteral("status"),
        identityStatusString(resolution.status)
    );
    if (resolution.method) {
        result.insert(
            QStringLiteral("method"),
            identityMethodString(*resolution.method)
        );
    }
    insertOptionalString(result, "query", resolution.query);
    if (resolution.confidence) {
        result.insert(
            QStringLiteral("confidence"),
            *resolution.confidence
        );
    }

    QJsonArray candidates;
    for (const cards::IdentityResolutionCandidate& candidate :
         resolution.candidates) {
        QJsonObject item;
        item.insert(
            QStringLiteral("identity"),
            identityJson(candidate.identity)
        );
        item.insert(QStringLiteral("score"), candidate.score);
        item.insert(
            QStringLiteral("reason"),
            QString::fromStdString(candidate.reason)
        );
        candidates.append(item);
    }

    result.insert(QStringLiteral("candidates"), candidates);
    result.insert(
        QStringLiteral("confirmed"),
        resolution.confirmed
    );
    return result;
}

cards::WorkingCardImportSource parseImportSource(
    const QJsonObject& object
)
{
    return cards::WorkingCardImportSource{
        .sourceId = stringValue(object, "sourceId"),
        .filename = optionalStringValue(object, "filename"),
        .importKind = stringValue(object, "importKind"),
        .entryKind = stringValue(object, "entryKind"),
        .identityHintOrigin =
            optionalStringValue(object, "identityHintOrigin"),
    };
}

QJsonObject importSourceJson(
    const cards::WorkingCardImportSource& source
)
{
    QJsonObject result;
    result.insert(
        QStringLiteral("sourceId"),
        QString::fromStdString(source.sourceId)
    );
    insertOptionalString(result, "filename", source.filename);
    result.insert(
        QStringLiteral("importKind"),
        QString::fromStdString(source.importKind)
    );
    result.insert(
        QStringLiteral("entryKind"),
        QString::fromStdString(source.entryKind)
    );
    insertOptionalString(
        result,
        "identityHintOrigin",
        source.identityHintOrigin
    );
    return result;
}

cards::CardIdentityHints parseIdentityHints(
    const QJsonObject& object
)
{
    return cards::CardIdentityHints{
        .name = optionalStringValue(object, "name"),
        .setCode = optionalStringValue(object, "setCode"),
        .collectorNumber =
            optionalStringValue(object, "collectorNumber"),
        .scryfallId = optionalStringValue(object, "scryfallId"),
        .language = optionalStringValue(object, "language"),
    };
}

QJsonObject identityHintsJson(
    const cards::CardIdentityHints& hints
)
{
    QJsonObject result;
    insertOptionalString(result, "name", hints.name);
    insertOptionalString(result, "setCode", hints.setCode);
    insertOptionalString(
        result,
        "collectorNumber",
        hints.collectorNumber
    );
    insertOptionalString(result, "scryfallId", hints.scryfallId);
    insertOptionalString(result, "language", hints.language);
    return result;
}

cards::BackLibraryAssetReference parseBackAsset(
    const QJsonObject& object
)
{
    return cards::BackLibraryAssetReference{
        .assetId = stringValue(object, "assetId"),
        .sha256 = stringValue(object, "sha256"),
        .format = stringValue(object, "format"),
    };
}

QJsonObject backAssetJson(
    const cards::BackLibraryAssetReference& asset
)
{
    QJsonObject result;
    result.insert(
        QStringLiteral("assetId"),
        QString::fromStdString(asset.assetId)
    );
    result.insert(
        QStringLiteral("sha256"),
        QString::fromStdString(asset.sha256)
    );
    result.insert(
        QStringLiteral("format"),
        QString::fromStdString(asset.format)
    );
    return result;
}

cards::MpcReferenceOrigin mpcOrigin(const QString& value)
{
    if (value == QStringLiteral("order-import")) {
        return cards::MpcReferenceOrigin::OrderImport;
    }
    if (value == QStringLiteral("gallery-selection")) {
        return cards::MpcReferenceOrigin::GallerySelection;
    }
    invalid("WorkingCard contains unsupported MPC reference origin.");
}

QString mpcOriginString(cards::MpcReferenceOrigin value)
{
    return value == cards::MpcReferenceOrigin::GallerySelection
        ? QStringLiteral("gallery-selection")
        : QStringLiteral("order-import");
}

cards::MpcProviderCardType mpcCardType(const QString& value)
{
    if (value == QStringLiteral("CARD")) {
        return cards::MpcProviderCardType::Card;
    }
    if (value == QStringLiteral("CARDBACK")) {
        return cards::MpcProviderCardType::Cardback;
    }
    invalid("WorkingCard contains unsupported MPC provider card type.");
}

QString mpcCardTypeString(cards::MpcProviderCardType value)
{
    return value == cards::MpcProviderCardType::Cardback
        ? QStringLiteral("CARDBACK")
        : QStringLiteral("CARD");
}

std::string displayNameFor(const cards::WorkingCard& card)
{
    if (card.identity && !card.identity->name.empty()) {
        return card.identity->name;
    }
    for (const cards::CardFace& face : card.faces) {
        if (
            face.side == cards::CardFaceSide::Front
            && face.name
            && !face.name->empty()
        ) {
            return *face.name;
        }
    }
    if (card.identityHints.name) {
        return *card.identityHints.name;
    }
    return card.id;
}

} // namespace

cards::WorkingCard parseWorkingCardDomain(
    const QJsonObject& raw
)
{
    cards::WorkingCard card{
        .id = stringValue(raw, "id"),
        .quantity = static_cast<std::uint32_t>(
            raw.value(QStringLiteral("quantity")).toDouble()
        ),
        .order = raw.value(QStringLiteral("order")).toInt(),
        .backMode = backMode(
            raw.value(QStringLiteral("backMode")).toString()
        ),
        .backModeSelectionPolicy = backPolicy(
            raw
                .value(QStringLiteral("backModeSelectionPolicy"))
                .toString()
        ),
        .section = optionalStringValue(raw, "section"),
        .importSource = parseImportSource(
            raw.value(QStringLiteral("importSource")).toObject()
        ),
        .identityHints = parseIdentityHints(
            raw.value(QStringLiteral("identityHints")).toObject()
        ),
        .identityResolution = parseIdentityResolution(
            raw.value(QStringLiteral("identityResolution")).toObject()
        ),
    };

    if (raw.value(QStringLiteral("identity")).isObject()) {
        card.identity = parseIdentity(
            raw.value(QStringLiteral("identity")).toObject()
        );
    }

    const QJsonArray faces =
        raw.value(QStringLiteral("faces")).toArray();
    card.faces.reserve(static_cast<std::size_t>(faces.size()));
    for (const QJsonValue& value : faces) {
        const QJsonObject object = value.toObject();
        cards::CardFace face{
            .id = stringValue(object, "id"),
            .side = faceSide(
                object.value(QStringLiteral("side")).toString()
            ),
            .name = optionalStringValue(object, "name"),
            .importedAssetId =
                optionalStringValue(object, "importedAssetId"),
        };
        if (object.contains(QStringLiteral("slots"))) {
            face.providerSlots =
                stringArray(object.value(QStringLiteral("slots")));
        }
        card.faces.push_back(std::move(face));
    }

    const QJsonObject selected =
        raw.value(QStringLiteral("selectedArtworkByFace")).toObject();
    if (selected.value(QStringLiteral("front")).isObject()) {
        card.selectedFrontArtwork = parseSelectedArtwork(
            selected.value(QStringLiteral("front")).toObject()
        );
    }
    if (selected.value(QStringLiteral("back")).isObject()) {
        card.selectedBackArtwork = parseSelectedArtwork(
            selected.value(QStringLiteral("back")).toObject()
        );
    }

    if (raw.value(QStringLiteral("manualBackAsset")).isObject()) {
        card.manualBackAsset = parseBackAsset(
            raw.value(QStringLiteral("manualBackAsset")).toObject()
        );
    }
    if (raw.value(QStringLiteral("manualBackArtwork")).isObject()) {
        card.manualBackArtwork = parseSelectedArtwork(
            raw.value(QStringLiteral("manualBackArtwork")).toObject()
        );
    }

    card.localArtworkIds =
        stringArray(raw.value(QStringLiteral("localArtworkIds")));

    const QJsonArray mpc =
        raw.value(QStringLiteral("mpcReferences")).toArray();
    card.mpcReferences.reserve(static_cast<std::size_t>(mpc.size()));
    for (const QJsonValue& value : mpc) {
        const QJsonObject object = value.toObject();
        cards::WorkingCardMpcReference reference{
            .faceId = faceSide(
                object.value(QStringLiteral("faceId")).toString()
            ),
            .importedAssetId =
                stringValue(object, "importedAssetId"),
            .providerAssetId =
                optionalStringValue(object, "providerAssetId"),
            .selectedArtworkId =
                optionalStringValue(object, "selectedArtworkId"),
            .availableLocally =
                object
                    .value(QStringLiteral("availableLocally"))
                    .toBool(),
        };

        if (object.contains(QStringLiteral("referenceOrigin"))) {
            reference.referenceOrigin = mpcOrigin(
                object
                    .value(QStringLiteral("referenceOrigin"))
                    .toString()
            );
        }
        if (object.contains(QStringLiteral("providerCardType"))) {
            reference.providerCardType = mpcCardType(
                object
                    .value(QStringLiteral("providerCardType"))
                    .toString()
            );
        }
        reference.providerSlots =
            stringArray(object.value(QStringLiteral("slots")));
        card.mpcReferences.push_back(std::move(reference));
    }

    if (raw.value(QStringLiteral("sharedMpcCardback")).isObject()) {
        const QJsonObject object =
            raw.value(QStringLiteral("sharedMpcCardback")).toObject();
        const QJsonObject provenance =
            object.value(QStringLiteral("provenance")).toObject();

        card.sharedMpcCardback =
            cards::WorkingCardSharedMpcCardback{
                .importedAssetId =
                    stringValue(object, "importedAssetId"),
                .providerAssetId =
                    optionalStringValue(object, "providerAssetId"),
                .selectedArtworkId =
                    optionalStringValue(object, "selectedArtworkId"),
                .originalFormat =
                    stringValue(object, "originalFormat"),
                .availableLocally =
                    object
                        .value(QStringLiteral("availableLocally"))
                        .toBool(),
                .provenance =
                    cards::SharedMpcCardbackProvenance{
                        .sourceId =
                            stringValue(provenance, "sourceId"),
                        .sourceFilename =
                            optionalStringValue(
                                provenance,
                                "sourceFilename"
                            ),
                    },
            };
    }

    const QJsonArray associations =
        raw.value(QStringLiteral("faceAssociations")).toArray();
    card.faceAssociations.reserve(
        static_cast<std::size_t>(associations.size())
    );
    for (const QJsonValue& value : associations) {
        const QJsonObject object = value.toObject();
        cards::WorkingCardFaceAssociation association{
            .slot = stringValue(object, "slot"),
            .frontAssetId =
                optionalStringValue(object, "frontAssetId"),
            .backAssetId =
                optionalStringValue(object, "backAssetId"),
            .reason = optionalStringValue(object, "reason"),
        };
        if (object.contains(QStringLiteral("confidence"))) {
            association.confidence =
                object.value(QStringLiteral("confidence")).toDouble();
        }
        if (object.contains(QStringLiteral("accepted"))) {
            association.accepted =
                object.value(QStringLiteral("accepted")).toBool();
        }
        card.faceAssociations.push_back(std::move(association));
    }

    card.displayName = displayNameFor(card);
    return card;
}

QJsonObject serializeWorkingCardDomain(
    const cards::WorkingCard& card
)
{
    QJsonObject raw;
    raw.insert(
        QStringLiteral("id"),
        QString::fromStdString(card.id)
    );
    raw.insert(
        QStringLiteral("quantity"),
        static_cast<double>(card.quantity)
    );
    raw.insert(QStringLiteral("order"), card.order);
    insertOptionalString(raw, "section", card.section);

    raw.insert(
        QStringLiteral("importSource"),
        importSourceJson(card.importSource)
    );
    raw.insert(
        QStringLiteral("identityHints"),
        identityHintsJson(card.identityHints)
    );
    raw.insert(
        QStringLiteral("identity"),
        card.identity
            ? QJsonValue(identityJson(*card.identity))
            : QJsonValue(QJsonValue::Null)
    );
    raw.insert(
        QStringLiteral("identityResolution"),
        identityResolutionJson(card.identityResolution)
    );

    QJsonArray faces;
    for (const cards::CardFace& face : card.faces) {
        QJsonObject object;
        object.insert(
            QStringLiteral("id"),
            QString::fromStdString(face.id)
        );
        object.insert(
            QStringLiteral("side"),
            faceSideString(face.side)
        );
        insertOptionalString(object, "name", face.name);
        insertOptionalString(
            object,
            "importedAssetId",
            face.importedAssetId
        );
        if (!face.providerSlots.empty()) {
            object.insert(
                QStringLiteral("slots"),
                stringArrayJson(face.providerSlots)
            );
        }
        faces.append(object);
    }
    raw.insert(QStringLiteral("faces"), faces);

    QJsonObject selected;
    if (card.selectedFrontArtwork) {
        selected.insert(
            QStringLiteral("front"),
            selectedArtworkJson(*card.selectedFrontArtwork)
        );
    }
    if (card.selectedBackArtwork) {
        selected.insert(
            QStringLiteral("back"),
            selectedArtworkJson(*card.selectedBackArtwork)
        );
    }
    raw.insert(QStringLiteral("selectedArtworkByFace"), selected);

    raw.insert(
        QStringLiteral("backMode"),
        backModeString(card.backMode)
    );
    raw.insert(
        QStringLiteral("backModeSelectionPolicy"),
        backPolicyString(card.backModeSelectionPolicy)
    );

    if (card.manualBackAsset) {
        raw.insert(
            QStringLiteral("manualBackAsset"),
            backAssetJson(*card.manualBackAsset)
        );
    }
    if (card.manualBackArtwork) {
        raw.insert(
            QStringLiteral("manualBackArtwork"),
            selectedArtworkJson(*card.manualBackArtwork)
        );
    }

    raw.insert(
        QStringLiteral("localArtworkIds"),
        stringArrayJson(card.localArtworkIds)
    );

    QJsonArray mpc;
    for (const cards::WorkingCardMpcReference& reference :
         card.mpcReferences) {
        QJsonObject object;
        object.insert(
            QStringLiteral("faceId"),
            faceSideString(reference.faceId)
        );
        object.insert(
            QStringLiteral("importedAssetId"),
            QString::fromStdString(reference.importedAssetId)
        );
        insertOptionalString(
            object,
            "providerAssetId",
            reference.providerAssetId
        );
        insertOptionalString(
            object,
            "selectedArtworkId",
            reference.selectedArtworkId
        );
        if (reference.referenceOrigin) {
            object.insert(
                QStringLiteral("referenceOrigin"),
                mpcOriginString(*reference.referenceOrigin)
            );
        }
        if (reference.providerCardType) {
            object.insert(
                QStringLiteral("providerCardType"),
                mpcCardTypeString(*reference.providerCardType)
            );
        }
        object.insert(
            QStringLiteral("slots"),
            stringArrayJson(reference.providerSlots)
        );
        object.insert(
            QStringLiteral("availableLocally"),
            reference.availableLocally
        );
        mpc.append(object);
    }
    raw.insert(QStringLiteral("mpcReferences"), mpc);

    if (card.sharedMpcCardback) {
        QJsonObject provenance;
        provenance.insert(
            QStringLiteral("sourceId"),
            QString::fromStdString(
                card.sharedMpcCardback->provenance.sourceId
            )
        );
        insertOptionalString(
            provenance,
            "sourceFilename",
            card.sharedMpcCardback->provenance.sourceFilename
        );

        QJsonObject object;
        object.insert(
            QStringLiteral("importedAssetId"),
            QString::fromStdString(
                card.sharedMpcCardback->importedAssetId
            )
        );
        insertOptionalString(
            object,
            "providerAssetId",
            card.sharedMpcCardback->providerAssetId
        );
        insertOptionalString(
            object,
            "selectedArtworkId",
            card.sharedMpcCardback->selectedArtworkId
        );
        object.insert(
            QStringLiteral("originalFormat"),
            QString::fromStdString(
                card.sharedMpcCardback->originalFormat
            )
        );
        object.insert(
            QStringLiteral("availableLocally"),
            card.sharedMpcCardback->availableLocally
        );
        object.insert(QStringLiteral("provenance"), provenance);
        raw.insert(QStringLiteral("sharedMpcCardback"), object);
    }

    QJsonArray associations;
    for (const cards::WorkingCardFaceAssociation& association :
         card.faceAssociations) {
        QJsonObject object;
        object.insert(
            QStringLiteral("slot"),
            QString::fromStdString(association.slot)
        );
        insertOptionalString(
            object,
            "frontAssetId",
            association.frontAssetId
        );
        insertOptionalString(
            object,
            "backAssetId",
            association.backAssetId
        );
        if (association.confidence) {
            object.insert(
                QStringLiteral("confidence"),
                *association.confidence
            );
        }
        insertOptionalString(object, "reason", association.reason);
        if (association.accepted) {
            object.insert(
                QStringLiteral("accepted"),
                *association.accepted
            );
        }
        associations.append(object);
    }
    raw.insert(QStringLiteral("faceAssociations"), associations);

    return raw;
}

} // namespace tcgprint::projects
