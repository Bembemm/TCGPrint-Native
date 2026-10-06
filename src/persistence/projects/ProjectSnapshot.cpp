#include "persistence/projects/ProjectSnapshot.h"
#include "persistence/projects/PersistedWorkingCardValidation.h"
#include "persistence/projects/ProjectSettingsCodec.h"

#include "domain/cards/WorkingCard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace tcgprint::projects {
namespace {

[[noreturn]] void invalid(const std::string& message)
{
    throw ProjectSnapshotError(
        ProjectSnapshotErrorCode::InvalidProjectSnapshot,
        message
    );
}

bool isIntegerNumber(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return false;
    }

    const double number = value.toDouble();
    return std::isfinite(number)
        && std::floor(number) == number;
}

int positiveSchemaVersion(const QJsonValue& value)
{
    if (!isIntegerNumber(value)) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::InvalidProjectSchemaVersion,
            "Project snapshot schema version must be a positive integer."
        );
    }

    const double number = value.toDouble();
    if (number < 1 || number > std::numeric_limits<int>::max()) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::InvalidProjectSchemaVersion,
            "Project snapshot schema version must be a positive integer."
        );
    }

    return static_cast<int>(number);
}

std::uint32_t positiveQuantity(const QJsonValue& value, const std::string& path)
{
    if (!isIntegerNumber(value)) {
        invalid(path + " must be a positive integer.");
    }

    const double number = value.toDouble();
    if (number < 1 || number > std::numeric_limits<std::uint32_t>::max()) {
        invalid(path + " must be a positive integer.");
    }

    return static_cast<std::uint32_t>(number);
}

int nonNegativeOrder(const QJsonValue& value, const std::string& path)
{
    if (!isIntegerNumber(value)) {
        invalid(path + " must be a non-negative integer.");
    }

    const double number = value.toDouble();
    if (number < 0 || number > std::numeric_limits<int>::max()) {
        invalid(path + " must be a non-negative integer.");
    }

    return static_cast<int>(number);
}

std::string requiredString(
    const QJsonObject& object,
    const char* key,
    const std::string& path
)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        invalid(path + "." + key + " must be a non-empty string.");
    }

    const QString text = value.toString();
    if (text.trimmed().isEmpty()) {
        invalid(path + "." + key + " must be a non-empty string.");
    }

    return text.toStdString();
}

void assertTopLevelShape(const QJsonObject& object, int version)
{
    const std::set<QString> allowed = version >= 6
        ? std::set<QString>{
            QStringLiteral("projectSchemaVersion"),
            QStringLiteral("cards"),
            QStringLiteral("settings"),
            QStringLiteral("physicalOrder"),
        }
        : std::set<QString>{
            QStringLiteral("projectSchemaVersion"),
            QStringLiteral("cards"),
            QStringLiteral("settings"),
        };

    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            invalid(
                "snapshot contains unsupported property "
                + iterator.key().toStdString()
                + "."
            );
        }
    }

    if (
        !object.contains(QStringLiteral("cards"))
        || !object.contains(QStringLiteral("settings"))
        || (version >= 6 && !object.contains(QStringLiteral("physicalOrder")))
    ) {
        invalid("snapshot is missing a required property.");
    }
}

QJsonObject defaultProjectSettings()
{
    QJsonObject trim;
    trim.insert(QStringLiteral("enabled"), false);
    trim.insert(QStringLiteral("extentMm"), 1.0);
    trim.insert(QStringLiteral("color"), QStringLiteral("blue"));

    QJsonObject external;
    external.insert(QStringLiteral("enabled"), false);
    external.insert(QStringLiteral("strokeWidthPt"), 0.3);
    external.insert(QStringLiteral("color"), QStringLiteral("black"));

    QJsonObject cutGuides;
    cutGuides.insert(QStringLiteral("trim"), trim);
    cutGuides.insert(QStringLiteral("external"), external);

    QJsonObject paperFormat;
    paperFormat.insert(QStringLiteral("name"), QStringLiteral("A4"));
    paperFormat.insert(QStringLiteral("widthMm"), 210.0);
    paperFormat.insert(QStringLiteral("heightMm"), 297.0);

    QJsonObject cardFormat;
    cardFormat.insert(QStringLiteral("id"), QStringLiteral("magic-standard"));
    cardFormat.insert(QStringLiteral("name"), QStringLiteral("Magic Standard"));
    cardFormat.insert(QStringLiteral("widthMm"), 63.5);
    cardFormat.insert(QStringLiteral("heightMm"), 88.9);
    cardFormat.insert(QStringLiteral("cornerRadiusMm"), 3.175);

    QJsonObject margins;
    margins.insert(QStringLiteral("top"), 0.0);
    margins.insert(QStringLiteral("right"), 0.0);
    margins.insert(QStringLiteral("bottom"), 0.0);
    margins.insert(QStringLiteral("left"), 0.0);

    QJsonObject registration;
    registration.insert(QStringLiteral("type"), QStringLiteral("none"));
    registration.insert(QStringLiteral("orientation"), QStringLiteral("portrait"));

    QJsonObject layout;
    layout.insert(QStringLiteral("skippedSlotIndices"), QJsonArray{});

    QJsonObject settings;
    settings.insert(QStringLiteral("bleedMm"), 0.625);
    settings.insert(QStringLiteral("roundedCorners"), false);
    settings.insert(QStringLiteral("cutGuides"), cutGuides);
    settings.insert(QStringLiteral("pageOrientation"), QStringLiteral("portrait"));
    settings.insert(QStringLiteral("cardOrientation"), QStringLiteral("portrait"));
    settings.insert(QStringLiteral("paperFormat"), paperFormat);
    settings.insert(QStringLiteral("cardFormat"), cardFormat);
    settings.insert(QStringLiteral("marginsMm"), margins);
    settings.insert(QStringLiteral("horizontalGapMm"), 0.0);
    settings.insert(QStringLiteral("verticalGapMm"), 0.0);
    settings.insert(QStringLiteral("registration"), registration);
    settings.insert(QStringLiteral("registrationOverride"), false);
    settings.insert(QStringLiteral("cutSourceSelection"), QJsonValue::Null);
    settings.insert(QStringLiteral("exportContentMode"), QStringLiteral("front-only"));
    settings.insert(QStringLiteral("missingBackPolicy"), QStringLiteral("use-project-default"));
    settings.insert(QStringLiteral("duplexFlipMode"), QStringLiteral("long-edge"));
    settings.insert(QStringLiteral("projectDefaultBack"), QJsonValue::Null);
    settings.insert(QStringLiteral("printerProfileSelection"), QJsonValue::Null);
    settings.insert(QStringLiteral("printerDuplexMode"), QStringLiteral("single-sided"));
    settings.insert(QStringLiteral("layout"), layout);
    return settings;
}

std::set<QString> settingsKeysForVersion(int version)
{
    std::set<QString> keys{
        QStringLiteral("bleedMm"),
        QStringLiteral("roundedCorners"),
        QStringLiteral("cutGuides"),
    };

    if (version >= 2) {
        keys.insert(QStringLiteral("pageOrientation"));
        keys.insert(QStringLiteral("cardOrientation"));
        keys.insert(QStringLiteral("paperFormat"));
        keys.insert(QStringLiteral("cardFormat"));
        keys.insert(QStringLiteral("marginsMm"));
        keys.insert(QStringLiteral("horizontalGapMm"));
        keys.insert(QStringLiteral("verticalGapMm"));
        keys.insert(QStringLiteral("registration"));
        keys.insert(QStringLiteral("registrationOverride"));
        keys.insert(QStringLiteral("layout"));
    }

    if (version >= 3) {
        keys.insert(QStringLiteral("cutSourceSelection"));
    }

    if (version >= 4) {
        keys.insert(QStringLiteral("exportContentMode"));
        keys.insert(QStringLiteral("missingBackPolicy"));
        keys.insert(QStringLiteral("duplexFlipMode"));
        keys.insert(QStringLiteral("projectDefaultBack"));
    }

    if (version >= 5) {
        keys.insert(QStringLiteral("printerProfileSelection"));
        keys.insert(QStringLiteral("printerDuplexMode"));
    }

    return keys;
}

QJsonObject mergeObject(
    const QJsonObject& base,
    const QJsonObject& overlay
)
{
    QJsonObject result = base;
    for (auto iterator = overlay.begin(); iterator != overlay.end(); ++iterator) {
        result.insert(iterator.key(), iterator.value());
    }
    return result;
}

QJsonObject normalizeProjectSettings(
    const QJsonObject& source,
    int sourceVersion
)
{
    const std::set<QString> allowed = settingsKeysForVersion(sourceVersion);

    for (auto iterator = source.begin(); iterator != source.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            invalid(
                "snapshot.settings contains unsupported property "
                + iterator.key().toStdString()
                + "."
            );
        }
    }

    QJsonObject result = defaultProjectSettings();

    for (auto iterator = source.begin(); iterator != source.end(); ++iterator) {
        if (
            iterator.key() == QStringLiteral("cutGuides")
            && iterator.value().isObject()
        ) {
            QJsonObject guides =
                result.value(QStringLiteral("cutGuides")).toObject();
            const QJsonObject overlay = iterator.value().toObject();

            if (overlay.value(QStringLiteral("trim")).isObject()) {
                guides.insert(
                    QStringLiteral("trim"),
                    mergeObject(
                        guides.value(QStringLiteral("trim")).toObject(),
                        overlay.value(QStringLiteral("trim")).toObject()
                    )
                );
            }

            if (overlay.value(QStringLiteral("external")).isObject()) {
                guides.insert(
                    QStringLiteral("external"),
                    mergeObject(
                        guides.value(QStringLiteral("external")).toObject(),
                        overlay.value(QStringLiteral("external")).toObject()
                    )
                );
            }

            result.insert(QStringLiteral("cutGuides"), guides);
            continue;
        }

        result.insert(iterator.key(), iterator.value());
    }

    return result;
}

std::set<QString> cardKeysForVersion(int version)
{
    std::set<QString> keys{
        QStringLiteral("id"),
        QStringLiteral("quantity"),
        QStringLiteral("order"),
        QStringLiteral("section"),
        QStringLiteral("importSource"),
        QStringLiteral("identityHints"),
        QStringLiteral("identity"),
        QStringLiteral("identityResolution"),
        QStringLiteral("faces"),
        QStringLiteral("selectedArtworkByFace"),
        QStringLiteral("localArtworkIds"),
        QStringLiteral("mpcReferences"),
        QStringLiteral("sharedMpcCardback"),
        QStringLiteral("faceAssociations"),
    };

    if (version >= 4) {
        keys.insert(QStringLiteral("backMode"));
        keys.insert(QStringLiteral("backModeSelectionPolicy"));
        keys.insert(QStringLiteral("manualBackAsset"));
        keys.insert(QStringLiteral("manualBackArtwork"));
    }

    return keys;
}

bool rawIdentityIsDoubleFaced(const QJsonValue& value)
{
    if (!value.isObject()) {
        return false;
    }

    const QJsonObject identity = value.toObject();
    const QJsonValue metadataValue =
        identity.value(QStringLiteral("metadata"));
    if (!metadataValue.isObject()) {
        return false;
    }

    const QJsonObject metadata = metadataValue.toObject();
    const QString layout =
        metadata.value(QStringLiteral("layout")).toString();

    const bool supportedLayout =
        layout == QStringLiteral("transform")
        || layout == QStringLiteral("modal_dfc")
        || layout == QStringLiteral("double_faced_token")
        || layout == QStringLiteral("reversible_card");

    if (!supportedLayout) {
        return false;
    }

    const QJsonValue facesValue =
        metadata.value(QStringLiteral("faces"));
    if (!facesValue.isArray()) {
        return false;
    }

    const QJsonArray faces = facesValue.toArray();
    if (faces.size() != 2) {
        return false;
    }

    for (const QJsonValue& faceValue : faces) {
        if (!faceValue.isObject()) {
            return false;
        }

        const QString name = faceValue
            .toObject()
            .value(QStringLiteral("name"))
            .toString();

        if (name.trimmed().isEmpty()) {
            return false;
        }
    }

    return true;
}

QString backModeString(cards::BackMode mode)
{
    switch (mode) {
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

cards::BackMode parseBackMode(
    const QJsonObject& raw,
    int version
)
{
    const QJsonValue value = raw.value(QStringLiteral("backMode"));

    if (version >= 4 && value.isString()) {
        const QString mode = value.toString();

        if (mode == QStringLiteral("auto")) {
            return cards::BackMode::Auto;
        }
        if (mode == QStringLiteral("project-default")) {
            return cards::BackMode::ProjectDefault;
        }
        if (mode == QStringLiteral("manual")) {
            return cards::BackMode::Manual;
        }
        if (mode == QStringLiteral("none")) {
            return cards::BackMode::None;
        }

        invalid("snapshot card backMode is not supported.");
    }

    const QJsonObject selected =
        raw.value(QStringLiteral("selectedArtworkByFace")).toObject();
    const QJsonObject selectedBack =
        selected.value(QStringLiteral("back")).toObject();

    const bool explicitBackArtwork =
        selectedBack.value(QStringLiteral("selectionPolicy")).toString()
        == QStringLiteral("user-selected");

    if (
        raw.contains(QStringLiteral("manualBackAsset"))
        || raw.contains(QStringLiteral("manualBackArtwork"))
        || explicitBackArtwork
    ) {
        return cards::BackMode::Manual;
    }

    if (rawIdentityIsDoubleFaced(raw.value(QStringLiteral("identity")))) {
        return cards::BackMode::Auto;
    }

    return cards::BackMode::ProjectDefault;
}

cards::BackModeSelectionPolicy parseBackModeSelectionPolicy(
    const QJsonObject& raw,
    cards::BackMode mode
)
{
    const QJsonValue value =
        raw.value(QStringLiteral("backModeSelectionPolicy"));

    if (value.isString()) {
        const QString policy = value.toString();

        if (policy == QStringLiteral("automatic")) {
            return cards::BackModeSelectionPolicy::Automatic;
        }
        if (policy == QStringLiteral("explicit")) {
            return cards::BackModeSelectionPolicy::Explicit;
        }

        invalid("snapshot card backModeSelectionPolicy is not supported.");
    }

    return mode == cards::BackMode::Manual
        ? cards::BackModeSelectionPolicy::Explicit
        : cards::BackModeSelectionPolicy::Automatic;
}

void validatePersistedCardRequiredShape(
    const QJsonObject& source,
    const std::string& path
)
{
    const std::array<const char*, 8> objectOrArrayKeys{
        "importSource",
        "identityHints",
        "identityResolution",
        "faces",
        "selectedArtworkByFace",
        "localArtworkIds",
        "mpcReferences",
        "faceAssociations",
    };

    for (const char* key : objectOrArrayKeys) {
        if (!source.contains(QLatin1String(key))) {
            invalid(path + "." + key + " is required in persisted WorkingCard data.");
        }
    }

    if (!source.value(QStringLiteral("importSource")).isObject()) {
        invalid(path + ".importSource must be an object.");
    }
    if (!source.value(QStringLiteral("identityHints")).isObject()) {
        invalid(path + ".identityHints must be an object.");
    }

    const QJsonValue identity = source.value(QStringLiteral("identity"));
    if (!source.contains(QStringLiteral("identity"))) {
        invalid(path + ".identity is required in persisted WorkingCard data.");
    }
    if (!identity.isNull() && !identity.isObject()) {
        invalid(path + ".identity must be null or an object.");
    }

    if (!source.value(QStringLiteral("identityResolution")).isObject()) {
        invalid(path + ".identityResolution must be an object.");
    }
    if (!source.value(QStringLiteral("faces")).isArray()) {
        invalid(path + ".faces must be an array.");
    }
    if (!source.value(QStringLiteral("selectedArtworkByFace")).isObject()) {
        invalid(path + ".selectedArtworkByFace must be an object.");
    }
    if (!source.value(QStringLiteral("localArtworkIds")).isArray()) {
        invalid(path + ".localArtworkIds must be an array.");
    }
    if (!source.value(QStringLiteral("mpcReferences")).isArray()) {
        invalid(path + ".mpcReferences must be an array.");
    }
    if (!source.value(QStringLiteral("faceAssociations")).isArray()) {
        invalid(path + ".faceAssociations must be an array.");
    }

    const QJsonArray faces = source.value(QStringLiteral("faces")).toArray();
    if (faces.isEmpty() || faces.size() > 2) {
        invalid(path + ".faces must contain one front face and at most one back face.");
    }

    bool hasFront = false;
    bool hasBack = false;

    for (const QJsonValue& faceValue : faces) {
        if (!faceValue.isObject()) {
            invalid(path + ".faces entries must be objects.");
        }

        const QJsonObject face = faceValue.toObject();
        const QString side = face.value(QStringLiteral("side")).toString();

        if (side == QStringLiteral("front")) {
            if (hasFront) {
                invalid(path + ".faces must not contain duplicate front faces.");
            }
            hasFront = true;
        } else if (side == QStringLiteral("back")) {
            if (hasBack) {
                invalid(path + ".faces must not contain duplicate back faces.");
            }
            hasBack = true;
        } else {
            invalid(path + ".faces side must be front or back.");
        }
    }

    if (!hasFront) {
        invalid(path + ".faces must contain a front face.");
    }
}

QJsonObject normalizeCardTopLevel(
    const QJsonObject& source,
    int version,
    cards::BackMode& backMode,
    cards::BackModeSelectionPolicy& selectionPolicy
)
{
    const std::set<QString> allowed = cardKeysForVersion(version);

    for (auto iterator = source.begin(); iterator != source.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            invalid(
                "snapshot card contains unsupported property "
                + iterator.key().toStdString()
                + "."
            );
        }
    }

    backMode = parseBackMode(source, version);
    selectionPolicy =
        parseBackModeSelectionPolicy(source, backMode);

    QJsonObject result = source;
    result.insert(
        QStringLiteral("backMode"),
        backModeString(backMode)
    );
    result.insert(
        QStringLiteral("backModeSelectionPolicy"),
        selectionPolicy == cards::BackModeSelectionPolicy::Explicit
            ? QStringLiteral("explicit")
            : QStringLiteral("automatic")
    );

    return result;
}

std::vector<PersistedWorkingCardCompat> parseCards(
    const QJsonValue& value,
    int version
)
{
    if (!value.isArray()) {
        invalid("snapshot.cards must be an array.");
    }

    const QJsonArray array = value.toArray();
    if (array.size() > 500) {
        invalid("snapshot.cards must contain at most 500 entries.");
    }

    std::vector<PersistedWorkingCardCompat> cards;
    cards.reserve(static_cast<std::size_t>(array.size()));

    std::set<std::string> ids;
    std::size_t physicalCount = 0;

    for (qsizetype index = 0; index < array.size(); ++index) {
        const QJsonValue value = array.at(index);
        if (!value.isObject()) {
            invalid(
                "snapshot.cards["
                + std::to_string(index)
                + "] must be an object."
            );
        }

        const QJsonObject source = value.toObject();
        const std::string path =
            "snapshot.cards[" + std::to_string(index) + "]";

        validatePersistedCardRequiredShape(source, path);
        validatePersistedWorkingCardReferences(source, version, path);

        cards::BackMode backMode;
        cards::BackModeSelectionPolicy selectionPolicy;
        const QJsonObject raw = normalizeCardTopLevel(
            source,
            version,
            backMode,
            selectionPolicy
        );
        const std::string id = requiredString(raw, "id", path);
        const std::uint32_t quantity =
            positiveQuantity(raw.value(QStringLiteral("quantity")), path + ".quantity");
        const int order =
            nonNegativeOrder(raw.value(QStringLiteral("order")), path + ".order");

        if (!ids.insert(id).second) {
            invalid("snapshot.cards must not contain duplicate WorkingCard IDs.");
        }

        physicalCount += quantity;
        if (physicalCount > cards::MaxPhysicalCardsPerExport) {
            invalid("snapshot.cards must contain at most 500 physical cards.");
        }

        cards.push_back(PersistedWorkingCardCompat{
            .id = id,
            .quantity = quantity,
            .order = order,
            .backMode = backMode,
            .backModeSelectionPolicy = selectionPolicy,
            .raw = raw,
        });
    }

    return cards;
}

std::vector<cards::WorkingCard> orderCards(
    const std::vector<PersistedWorkingCardCompat>& cards
)
{
    std::vector<cards::WorkingCard> result;
    result.reserve(cards.size());

    for (const PersistedWorkingCardCompat& card : cards) {
        result.push_back(cards::WorkingCard{
            .id = card.id,
            .quantity = card.quantity,
            .order = card.order,
        });
    }

    return result;
}

cards::PhysicalOrder parsePhysicalOrder(
    const QJsonValue& value,
    const std::vector<cards::WorkingCard>& workingCards
)
{
    if (!value.isObject()) {
        invalid("snapshot.physicalOrder must be an object.");
    }

    const QJsonObject object = value.toObject();
    if (
        object.size() != 2
        || !object.contains(QStringLiteral("nextInstanceId"))
        || !object.contains(QStringLiteral("instances"))
    ) {
        invalid(
            "snapshot.physicalOrder must contain only nextInstanceId and instances."
        );
    }

    const QJsonValue nextValue = object.value(QStringLiteral("nextInstanceId"));
    if (!isIntegerNumber(nextValue)) {
        invalid("snapshot.physicalOrder.nextInstanceId must be a positive integer.");
    }

    const double nextNumber = nextValue.toDouble();
    if (
        nextNumber < 1
        || nextNumber > static_cast<double>(cards::MaxPersistedPhysicalInstanceId)
    ) {
        invalid("snapshot.physicalOrder.nextInstanceId must be a positive integer.");
    }

    const QJsonValue instancesValue = object.value(QStringLiteral("instances"));
    if (!instancesValue.isArray()) {
        invalid("snapshot.physicalOrder.instances must be an array.");
    }

    const QJsonArray array = instancesValue.toArray();
    std::vector<cards::PhysicalInstanceRef> instances;
    instances.reserve(static_cast<std::size_t>(array.size()));

    for (qsizetype index = 0; index < array.size(); ++index) {
        const QJsonValue item = array.at(index);
        if (!item.isObject()) {
            invalid("snapshot.physicalOrder.instances entry must be an object.");
        }

        const QJsonObject reference = item.toObject();
        if (
            reference.size() != 2
            || !reference.contains(QStringLiteral("id"))
            || !reference.contains(QStringLiteral("workingCardId"))
        ) {
            invalid(
                "snapshot.physicalOrder.instances entry must contain only id and workingCardId."
            );
        }

        const std::string persistedId =
            requiredString(reference, "id", "snapshot.physicalOrder.instances");
        const auto parsedId = cards::parsePhysicalInstanceIdString(persistedId);

        if (!parsedId) {
            invalid("snapshot.physicalOrder contains a malformed physical instance ID.");
        }

        instances.push_back(cards::PhysicalInstanceRef{
            .id = *parsedId,
            .workingCardId = requiredString(
                reference,
                "workingCardId",
                "snapshot.physicalOrder.instances"
            ),
        });
    }

    try {
        return cards::validatePhysicalOrder(
            workingCards,
            cards::PhysicalOrder{
                .nextInstanceId =
                    static_cast<cards::PhysicalInstanceId>(nextNumber),
                .instances = std::move(instances),
            }
        );
    } catch (const cards::PhysicalOrderError& error) {
        invalid(
            std::string("snapshot.physicalOrder ")
            + error.what()
        );
    }
}

} // namespace

ProjectSnapshotError::ProjectSnapshotError(
    ProjectSnapshotErrorCode code,
    std::string message
)
    : std::runtime_error(std::move(message)),
      code_(code)
{
}

ProjectSnapshotErrorCode ProjectSnapshotError::code() const noexcept
{
    return code_;
}

QByteArray serializeProjectSnapshot(const ProjectSnapshotCompat& snapshot)
{
    std::vector<cards::WorkingCard> workingCards;
    workingCards.reserve(snapshot.cards.size());

    QJsonArray persistedCards;
    for (const PersistedWorkingCardCompat& card : snapshot.cards) {
        QJsonObject raw = card.raw;
        raw.insert(QStringLiteral("id"), QString::fromStdString(card.id));
        raw.insert(QStringLiteral("quantity"), static_cast<double>(card.quantity));
        raw.insert(QStringLiteral("order"), card.order);
        persistedCards.append(raw);

        workingCards.push_back(cards::WorkingCard{
            .id = card.id,
            .quantity = card.quantity,
            .order = card.order,
        });
    }

    cards::PhysicalOrder canonicalOrder;
    try {
        canonicalOrder = cards::validatePhysicalOrder(
            workingCards,
            snapshot.physicalOrder
        );
    } catch (const cards::PhysicalOrderError& error) {
        invalid(std::string("snapshot.physicalOrder ") + error.what());
    }

    QJsonArray instances;
    for (const cards::PhysicalInstanceRef& instance : canonicalOrder.instances) {
        QJsonObject reference;
        reference.insert(
            QStringLiteral("id"),
            QString::fromStdString(cards::physicalInstanceIdString(instance.id))
        );
        reference.insert(
            QStringLiteral("workingCardId"),
            QString::fromStdString(instance.workingCardId)
        );
        instances.append(reference);
    }

    QJsonObject physicalOrder;
    physicalOrder.insert(
        QStringLiteral("nextInstanceId"),
        static_cast<double>(canonicalOrder.nextInstanceId)
    );
    physicalOrder.insert(QStringLiteral("instances"), instances);

    QJsonObject root;
    root.insert(
        QStringLiteral("projectSchemaVersion"),
        CurrentProjectSchemaVersion
    );
    root.insert(QStringLiteral("cards"), persistedCards);
    const QJsonObject normalizedSettings =
        normalizeProjectSettings(
            snapshot.settings,
            CurrentProjectSchemaVersion
        );
    root.insert(
        QStringLiteral("settings"),
        overlayProjectPrintSettings(
            normalizedSettings,
            snapshot.printSettings
        )
    );
    root.insert(QStringLiteral("physicalOrder"), physicalOrder);

    const QByteArray serialized =
        QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (static_cast<std::size_t>(serialized.size()) > MaxProjectSnapshotBytes) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::ProjectSnapshotTooLarge,
            "Project snapshot exceeds the 16 MiB limit."
        );
    }

    return serialized;
}

ProjectSnapshotCompat deserializeProjectSnapshot(const QByteArray& json)
{
    if (static_cast<std::size_t>(json.size()) > MaxProjectSnapshotBytes) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::ProjectSnapshotTooLarge,
            "Project snapshot exceeds the 16 MiB limit."
        );
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);

    if (
        parseError.error != QJsonParseError::NoError
        || !document.isObject()
    ) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::InvalidProjectSnapshot,
            "Project snapshot must contain valid JSON object data."
        );
    }

    const QJsonObject root = document.object();
    const int version =
        positiveSchemaVersion(root.value(QStringLiteral("projectSchemaVersion")));

    if (version > CurrentProjectSchemaVersion) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::FutureProjectSchemaVersion,
            "Project snapshot schema is newer than this application supports."
        );
    }

    if (version < 1 || version > CurrentProjectSchemaVersion) {
        throw ProjectSnapshotError(
            ProjectSnapshotErrorCode::UnsupportedProjectSchemaVersion,
            "Project snapshot schema has no supported migration path."
        );
    }

    assertTopLevelShape(root, version);

    const std::vector<PersistedWorkingCardCompat> persistedCards =
        parseCards(
            root.value(QStringLiteral("cards")),
            version
        );
    const std::vector<cards::WorkingCard> workingCards =
        orderCards(persistedCards);

    const QJsonValue settingsValue = root.value(QStringLiteral("settings"));
    if (!settingsValue.isObject()) {
        invalid("snapshot.settings must be an object.");
    }

    cards::PhysicalOrder physicalOrder;
    if (version >= 6) {
        physicalOrder = parsePhysicalOrder(
            root.value(QStringLiteral("physicalOrder")),
            workingCards
        );
    } else {
        physicalOrder = cards::makeLegacyPhysicalOrder(workingCards);
    }

    const QJsonObject normalizedSettings =
        normalizeProjectSettings(
            settingsValue.toObject(),
            version
        );

    return ProjectSnapshotCompat{
        .sourceSchemaVersion = version,
        .projectSchemaVersion = CurrentProjectSchemaVersion,
        .cards = persistedCards,
        .settings = normalizedSettings,
        .printSettings =
            parseProjectPrintSettings(normalizedSettings),
        .physicalOrder = std::move(physicalOrder),
    };
}

} // namespace tcgprint::projects
