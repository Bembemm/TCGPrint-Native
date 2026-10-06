#include "persistence/projects/ProjectSnapshot.h"

#include "domain/cards/WorkingCard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

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

std::vector<PersistedWorkingCardCompat> parseCards(const QJsonValue& value)
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

        const QJsonObject raw = value.toObject();
        const std::string path =
            "snapshot.cards[" + std::to_string(index) + "]";

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
        parseCards(root.value(QStringLiteral("cards")));
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

    return ProjectSnapshotCompat{
        .sourceSchemaVersion = version,
        .projectSchemaVersion = CurrentProjectSchemaVersion,
        .cards = persistedCards,
        .settings = settingsValue.toObject(),
        .physicalOrder = std::move(physicalOrder),
    };
}

} // namespace tcgprint::projects
