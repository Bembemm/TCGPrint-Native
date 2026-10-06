#include "persistence/projects/PersistedWorkingCardValidation.h"

#include "persistence/projects/ProjectSnapshot.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <optional>
#include <set>
#include <string_view>

namespace tcgprint::projects {
namespace {

[[noreturn]] void invalid(const std::string& message)
{
    throw ProjectSnapshotError(
        ProjectSnapshotErrorCode::InvalidProjectSnapshot,
        message
    );
}

void rejectUnsupportedKeys(
    const QJsonObject& object,
    const std::set<QString>& allowed,
    const std::string& path
)
{
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            invalid(
                path
                + " contains unsupported property "
                + iterator.key().toStdString()
                + "."
            );
        }
    }
}

QString requiredString(
    const QJsonObject& object,
    const char* key,
    const std::string& path,
    int maximum
)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        invalid(path + "." + key + " must be a non-empty string.");
    }

    const QString text = value.toString();
    if (
        text.trimmed().isEmpty()
        || text.size() > maximum
        || text.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1F]")))
    ) {
        invalid(path + "." + key + " must be a non-empty bounded string.");
    }

    return text;
}

void optionalString(
    const QJsonObject& object,
    const char* key,
    const std::string& path,
    int maximum
)
{
    if (!object.contains(QLatin1String(key))) {
        return;
    }

    static_cast<void>(requiredString(object, key, path, maximum));
}

bool isSafeArtworkCandidateId(const QString& value)
{
    static const QRegularExpression Pattern(
        QStringLiteral(
            "^(upload:[a-f0-9]{64}|"
            "scryfall:[a-f0-9-]{36}:(front|back)|"
            "mpc:[a-f0-9]{64})$"
        )
    );

    return Pattern.match(value).hasMatch();
}

void validateImportSource(
    const QJsonObject& source,
    const std::string& path
)
{
    rejectUnsupportedKeys(
        source,
        {
            QStringLiteral("sourceId"),
            QStringLiteral("filename"),
            QStringLiteral("importKind"),
            QStringLiteral("entryKind"),
            QStringLiteral("identityHintOrigin"),
        },
        path
    );

    static_cast<void>(requiredString(source, "sourceId", path, 180));
    static_cast<void>(requiredString(source, "importKind", path, 60));
    static_cast<void>(requiredString(source, "entryKind", path, 60));
    optionalString(source, "filename", path, 240);

    if (source.contains(QStringLiteral("identityHintOrigin"))) {
        const QString origin =
            requiredString(source, "identityHintOrigin", path, 32);
        if (origin != QStringLiteral("explicit-card-hint")) {
            invalid(path + ".identityHintOrigin is not supported.");
        }
    }
}

void validateIdentityHints(
    const QJsonObject& hints,
    const std::string& path
)
{
    rejectUnsupportedKeys(
        hints,
        {
            QStringLiteral("name"),
            QStringLiteral("setCode"),
            QStringLiteral("collectorNumber"),
            QStringLiteral("scryfallId"),
            QStringLiteral("language"),
        },
        path
    );

    optionalString(hints, "name", path, 200);
    optionalString(hints, "setCode", path, 12);
    optionalString(hints, "collectorNumber", path, 40);
    optionalString(hints, "scryfallId", path, 80);
    optionalString(hints, "language", path, 12);
}

std::set<QString> validateFaces(
    const QJsonArray& faces,
    const std::string& path
)
{
    if (faces.isEmpty() || faces.size() > 2) {
        invalid(path + " must contain one front face and at most one back face.");
    }

    std::set<QString> sides;

    for (qsizetype index = 0; index < faces.size(); ++index) {
        const QJsonValue value = faces.at(index);
        if (!value.isObject()) {
            invalid(
                path
                + "["
                + std::to_string(index)
                + "] must be an object."
            );
        }

        const QJsonObject face = value.toObject();
        const std::string facePath =
            path + "[" + std::to_string(index) + "]";

        rejectUnsupportedKeys(
            face,
            {
                QStringLiteral("id"),
                QStringLiteral("side"),
                QStringLiteral("name"),
                QStringLiteral("importedAssetId"),
                QStringLiteral("slots"),
            },
            facePath
        );

        const QString side =
            requiredString(face, "side", facePath, 8);
        const QString id =
            requiredString(face, "id", facePath, 8);

        if (
            side != QStringLiteral("front")
            && side != QStringLiteral("back")
        ) {
            invalid(facePath + ".side must be front or back.");
        }

        if (id != side) {
            invalid(facePath + ".id must match its face side.");
        }

        if (!sides.insert(side).second) {
            invalid(path + " must not contain duplicate face sides.");
        }

        optionalString(face, "name", facePath, 200);
        optionalString(face, "importedAssetId", facePath, 128);

        if (face.contains(QStringLiteral("slots"))) {
            const QJsonValue slotsValue =
                face.value(QStringLiteral("slots"));
            if (!slotsValue.isArray()) {
                invalid(facePath + ".slots must be an array.");
            }

            const QJsonArray slots = slotsValue.toArray();
            if (slots.size() > 100) {
                invalid(facePath + ".slots contains too many entries.");
            }

            for (qsizetype slot = 0; slot < slots.size(); ++slot) {
                if (
                    !slots.at(slot).isString()
                    || slots.at(slot).toString().trimmed().isEmpty()
                    || slots.at(slot).toString().size() > 64
                ) {
                    invalid(facePath + ".slots contains an invalid slot.");
                }
            }
        }
    }

    if (!sides.contains(QStringLiteral("front"))) {
        invalid(path + " must contain a front face.");
    }

    return sides;
}

void validateSelectedArtwork(
    const QJsonObject& artwork,
    const std::optional<QString>& expectedSide,
    const std::string& path
)
{
    rejectUnsupportedKeys(
        artwork,
        {
            QStringLiteral("candidateId"),
            QStringLiteral("source"),
            QStringLiteral("identityId"),
            QStringLiteral("faceId"),
            QStringLiteral("providerAssetId"),
            QStringLiteral("selectedArtworkId"),
            QStringLiteral("selectionPolicy"),
        },
        path
    );

    const QString candidateId =
        requiredString(artwork, "candidateId", path, 128);
    if (!isSafeArtworkCandidateId(candidateId)) {
        invalid(path + ".candidateId is not a supported artwork candidate ID.");
    }

    const QString source =
        requiredString(artwork, "source", path, 16);
    if (
        source != QStringLiteral("scryfall")
        && source != QStringLiteral("upload")
        && source != QStringLiteral("mpc")
        && source != QStringLiteral("url")
        && source != QStringLiteral("custom")
    ) {
        invalid(path + ".source is not supported.");
    }

    if (!artwork.contains(QStringLiteral("identityId"))) {
        invalid(path + ".identityId is required.");
    }

    const QJsonValue identityId =
        artwork.value(QStringLiteral("identityId"));
    if (!identityId.isNull() && !identityId.isString()) {
        invalid(path + ".identityId must be a string or null.");
    }
    if (identityId.isString()) {
        static_cast<void>(requiredString(artwork, "identityId", path, 180));
    }

    const QString faceId =
        requiredString(artwork, "faceId", path, 8);
    if (
        faceId != QStringLiteral("front")
        && faceId != QStringLiteral("back")
    ) {
        invalid(path + ".faceId must be front or back.");
    }

    if (expectedSide && faceId != *expectedSide) {
        invalid(path + ".faceId must match the artwork selection key.");
    }

    optionalString(artwork, "providerAssetId", path, 200);
    optionalString(artwork, "selectedArtworkId", path, 200);
    optionalString(artwork, "selectionPolicy", path, 80);
}

void validateArtworkSelections(
    const QJsonObject& selected,
    const std::set<QString>& faceSides,
    const std::string& path
)
{
    rejectUnsupportedKeys(
        selected,
        {
            QStringLiteral("front"),
            QStringLiteral("back"),
        },
        path
    );

    for (const QString& side : {
        QStringLiteral("front"),
        QStringLiteral("back"),
    }) {
        if (!selected.contains(side)) {
            continue;
        }

        if (!faceSides.contains(side)) {
            invalid(
                path
                + "."
                + side.toStdString()
                + " cannot select artwork for a missing face."
            );
        }

        const QJsonValue value = selected.value(side);
        if (!value.isObject()) {
            invalid(
                path
                + "."
                + side.toStdString()
                + " must be an object."
            );
        }

        validateSelectedArtwork(
            value.toObject(),
            side,
            path + "." + side.toStdString()
        );
    }
}

void validateLocalArtworkIds(
    const QJsonArray& ids,
    const std::string& path
)
{
    if (ids.size() > 200) {
        invalid(path + " contains too many entries.");
    }

    for (qsizetype index = 0; index < ids.size(); ++index) {
        if (!ids.at(index).isString()) {
            invalid(path + " contains a non-string artwork ID.");
        }

        const QString id = ids.at(index).toString();
        if (
            !isSafeArtworkCandidateId(id)
            || !id.startsWith(QStringLiteral("upload:"))
        ) {
            invalid(path + " contains an invalid local upload artwork ID.");
        }
    }
}

} // namespace

void validatePersistedWorkingCardReferences(
    const QJsonObject& source,
    int schemaVersion,
    const std::string& path
)
{
    static_cast<void>(schemaVersion);

    validateImportSource(
        source.value(QStringLiteral("importSource")).toObject(),
        path + ".importSource"
    );

    validateIdentityHints(
        source.value(QStringLiteral("identityHints")).toObject(),
        path + ".identityHints"
    );

    const std::set<QString> faceSides = validateFaces(
        source.value(QStringLiteral("faces")).toArray(),
        path + ".faces"
    );

    validateArtworkSelections(
        source.value(QStringLiteral("selectedArtworkByFace")).toObject(),
        faceSides,
        path + ".selectedArtworkByFace"
    );

    validateLocalArtworkIds(
        source.value(QStringLiteral("localArtworkIds")).toArray(),
        path + ".localArtworkIds"
    );

    if (source.contains(QStringLiteral("manualBackArtwork"))) {
        const QJsonValue value =
            source.value(QStringLiteral("manualBackArtwork"));
        if (!value.isObject()) {
            invalid(path + ".manualBackArtwork must be an object.");
        }

        const QJsonObject artwork = value.toObject();
        validateSelectedArtwork(
            artwork,
            std::nullopt,
            path + ".manualBackArtwork"
        );

        if (
            artwork.value(QStringLiteral("selectionPolicy")).toString()
            != QStringLiteral("user-selected")
        ) {
            invalid(
                path
                + ".manualBackArtwork.selectionPolicy must be user-selected."
            );
        }
    }
}

} // namespace tcgprint::projects
