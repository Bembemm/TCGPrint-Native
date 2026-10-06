#include "persistence/projects/ProjectSettingsReferenceValidation.h"

#include "persistence/projects/ProjectSnapshot.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <utility>
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

void exactKeys(
    const QJsonObject& object,
    const std::set<QString>& allowed,
    const std::string& path
)
{
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            invalid(
                path + " contains unsupported property "
                + iterator.key().toStdString() + "."
            );
        }
    }
}

QString boundedString(
    const QJsonObject& object,
    const char* key,
    const std::string& path,
    int maximum,
    bool allowEmpty = false
)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString()) {
        invalid(path + "." + key + " must be a string.");
    }

    const QString text = value.toString().trimmed();
    if ((!allowEmpty && text.isEmpty()) || text.size() > maximum) {
        invalid(path + "." + key + " has invalid length.");
    }

    return text;
}

double boundedNumber(
    const QJsonValue& value,
    const std::string& path,
    double minimum,
    double maximum
)
{
    if (!value.isDouble()) {
        invalid(path + " must be a number.");
    }

    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum) {
        invalid(path + " is outside the supported range.");
    }

    return number;
}

void validateBackReference(
    const QJsonObject& asset,
    const std::string& path
)
{
    exactKeys(
        asset,
        {
            QStringLiteral("assetId"),
            QStringLiteral("sha256"),
            QStringLiteral("format"),
        },
        path
    );

    const QString sha = boundedString(asset, "sha256", path, 64);
    static const QRegularExpression Sha(
        QStringLiteral("^[a-f0-9]{64}$")
    );
    if (!Sha.match(sha).hasMatch()) {
        invalid(path + ".sha256 must be a lowercase SHA-256 digest.");
    }

    if (
        boundedString(asset, "assetId", path, 100)
        != QStringLiteral("back:") + sha
    ) {
        invalid(path + ".assetId must match its content hash.");
    }

    const QString format = boundedString(asset, "format", path, 8);
    if (format != QStringLiteral("jpeg") && format != QStringLiteral("png")) {
        invalid(path + ".format must be jpeg or png.");
    }
}

void validateCutSourceSelection(const QJsonObject& selection)
{
    const std::string path = "snapshot.settings.cutSourceSelection";
    exactKeys(
        selection,
        {
            QStringLiteral("fileId"),
            QStringLiteral("fileHash"),
            QStringLiteral("dxfUnitsOverride"),
        },
        path
    );

    static_cast<void>(
        boundedString(selection, "fileId", path, 180)
    );

    const QString hash = boundedString(selection, "fileHash", path, 64);
    static const QRegularExpression Sha(
        QStringLiteral("^[a-f0-9]{64}$")
    );
    if (!Sha.match(hash).hasMatch()) {
        invalid(path + ".fileHash must be a lowercase SHA-256 digest.");
    }

    if (selection.contains(QStringLiteral("dxfUnitsOverride"))) {
        const QString units =
            boundedString(selection, "dxfUnitsOverride", path, 4);
        static const std::set<QString> Units{
            QStringLiteral("mm"),
            QStringLiteral("cm"),
            QStringLiteral("m"),
            QStringLiteral("in"),
            QStringLiteral("ft"),
            QStringLiteral("yd"),
        };
        if (!Units.contains(units)) {
            invalid(path + ".dxfUnitsOverride is not supported.");
        }
    }
}

void validateSideCalibration(
    const QJsonObject& side,
    const std::string& path
)
{
    exactKeys(
        side,
        {
            QStringLiteral("offsetXUm"),
            QStringLiteral("offsetYUm"),
            QStringLiteral("rotationDeg"),
            QStringLiteral("scaleX"),
            QStringLiteral("scaleY"),
            QStringLiteral("skewXDeg"),
            QStringLiteral("skewYDeg"),
        },
        path
    );

    for (const char* key : {"offsetXUm", "offsetYUm"}) {
        const QJsonValue value = side.value(QLatin1String(key));
        if (
            !value.isDouble()
            || std::floor(value.toDouble()) != value.toDouble()
            || value.toDouble() < -10000
            || value.toDouble() > 10000
        ) {
            invalid(path + "." + key + " must be integer micrometers within ±10000.");
        }
    }

    static_cast<void>(
        boundedNumber(
            side.value(QStringLiteral("rotationDeg")),
            path + ".rotationDeg",
            -5.0,
            5.0
        )
    );
    static_cast<void>(
        boundedNumber(
            side.value(QStringLiteral("scaleX")),
            path + ".scaleX",
            0.98,
            1.02
        )
    );
    static_cast<void>(
        boundedNumber(
            side.value(QStringLiteral("scaleY")),
            path + ".scaleY",
            0.98,
            1.02
        )
    );

    for (const char* key : {"skewXDeg", "skewYDeg"}) {
        if (side.contains(QLatin1String(key))) {
            static_cast<void>(
                boundedNumber(
                    side.value(QLatin1String(key)),
                    path + "." + key,
                    -1.5,
                    1.5
                )
            );
        }
    }
}

void validatePhysicalVerification(
    const QJsonObject& verification,
    const std::string& path
)
{
    exactKeys(
        verification,
        {
            QStringLiteral("sessionId"),
            QStringLiteral("verifiedAt"),
            QStringLiteral("measurements"),
            QStringLiteral("residualSummaryMm"),
        },
        path
    );

    const QString sessionId =
        boundedString(verification, "sessionId", path, 128);
    static const QRegularExpression SessionId(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
    );
    if (!SessionId.match(sessionId).hasMatch()) {
        invalid(path + ".sessionId is invalid.");
    }

    const QString verifiedAt =
        boundedString(verification, "verifiedAt", path, 40);
    QDateTime timestamp =
        QDateTime::fromString(verifiedAt, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(verifiedAt, Qt::ISODate);
    }
    if (!timestamp.isValid()) {
        invalid(path + ".verifiedAt must be an ISO timestamp.");
    }

    const QJsonValue measurementsValue =
        verification.value(QStringLiteral("measurements"));
    if (!measurementsValue.isArray()) {
        invalid(path + ".measurements must be an array.");
    }

    const QJsonArray measurements = measurementsValue.toArray();
    if (measurements.isEmpty() || measurements.size() > 5) {
        invalid(path + ".measurements must contain one through five targets.");
    }

    static const std::set<QString> PointIds{
        QStringLiteral("center"),
        QStringLiteral("top-left"),
        QStringLiteral("top-right"),
        QStringLiteral("bottom-left"),
        QStringLiteral("bottom-right"),
    };

    std::set<QString> seen;
    std::vector<double> magnitudes;

    for (qsizetype index = 0; index < measurements.size(); ++index) {
        if (!measurements.at(index).isObject()) {
            invalid(path + ".measurements entries must be objects.");
        }

        const QJsonObject measurement =
            measurements.at(index).toObject();
        const std::string itemPath =
            path + ".measurements[" + std::to_string(index) + "]";

        exactKeys(
            measurement,
            {
                QStringLiteral("pointId"),
                QStringLiteral("residualXUm"),
                QStringLiteral("residualYUm"),
            },
            itemPath
        );

        const QString pointId =
            boundedString(measurement, "pointId", itemPath, 20);
        if (!PointIds.contains(pointId) || !seen.insert(pointId).second) {
            invalid(itemPath + ".pointId is invalid or duplicated.");
        }

        const double x = measurement.value(QStringLiteral("residualXUm")).toDouble(
            std::numeric_limits<double>::quiet_NaN()
        );
        const double y = measurement.value(QStringLiteral("residualYUm")).toDouble(
            std::numeric_limits<double>::quiet_NaN()
        );
        if (
            !std::isfinite(x)
            || !std::isfinite(y)
            || std::floor(x) != x
            || std::floor(y) != y
            || std::abs(x) > 10000
            || std::abs(y) > 10000
        ) {
            invalid(itemPath + " residuals must be integer micrometers within ±10000.");
        }

        magnitudes.push_back(std::hypot(x, y) / 1000.0);
    }

    const QJsonValue summaryValue =
        verification.value(QStringLiteral("residualSummaryMm"));
    if (!summaryValue.isObject()) {
        invalid(path + ".residualSummaryMm must be an object.");
    }

    const QJsonObject summary = summaryValue.toObject();
    exactKeys(
        summary,
        {
            QStringLiteral("mean"),
            QStringLiteral("minimum"),
            QStringLiteral("maximum"),
        },
        path + ".residualSummaryMm"
    );

    const double expectedMean =
        std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0)
        / static_cast<double>(magnitudes.size());
    const double expectedMinimum =
        *std::min_element(magnitudes.begin(), magnitudes.end());
    const double expectedMaximum =
        *std::max_element(magnitudes.begin(), magnitudes.end());

    for (const auto& [key, expected] :
         std::array<std::pair<const char*, double>, 3>{
             std::pair{"mean", expectedMean},
             std::pair{"minimum", expectedMinimum},
             std::pair{"maximum", expectedMaximum},
         }) {
        const double received =
            summary.value(QLatin1String(key)).toDouble(
                std::numeric_limits<double>::quiet_NaN()
            );
        if (
            !std::isfinite(received)
            || std::abs(received - expected) > 1e-12
        ) {
            invalid(path + ".residualSummaryMm." + key + " does not match measurements.");
        }
    }
}

void validatePrinterProfileSnapshot(const QJsonObject& profile)
{
    const std::string path = "snapshot.settings.printerProfileSelection";

    exactKeys(
        profile,
        {
            QStringLiteral("id"),
            QStringLiteral("name"),
            QStringLiteral("front"),
            QStringLiteral("back"),
            QStringLiteral("paperSize"),
            QStringLiteral("paperWidthMm"),
            QStringLiteral("paperHeightMm"),
            QStringLiteral("pageOrientation"),
            QStringLiteral("duplexMode"),
            QStringLiteral("mediaType"),
            QStringLiteral("printQualityProfile"),
            QStringLiteral("feedSource"),
            QStringLiteral("notes"),
            QStringLiteral("physicalValidationStatus"),
            QStringLiteral("physicalVerification"),
            QStringLiteral("version"),
            QStringLiteral("profileHash"),
        },
        path
    );

    const QString id = boundedString(profile, "id", path, 128);
    static const QRegularExpression Id(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
    );
    if (!Id.match(id).hasMatch()) {
        invalid(path + ".id is invalid.");
    }

    static_cast<void>(boundedString(profile, "name", path, 160));
    static_cast<void>(boundedString(profile, "paperSize", path, 80));

    for (const char* key : {"front", "back"}) {
        const QJsonValue value = profile.value(QLatin1String(key));
        if (!value.isObject()) {
            invalid(path + "." + key + " must be an object.");
        }
        validateSideCalibration(
            value.toObject(),
            path + "." + key
        );
    }

    static_cast<void>(
        boundedNumber(
            profile.value(QStringLiteral("paperWidthMm")),
            path + ".paperWidthMm",
            std::numeric_limits<double>::min(),
            2000.0
        )
    );
    static_cast<void>(
        boundedNumber(
            profile.value(QStringLiteral("paperHeightMm")),
            path + ".paperHeightMm",
            std::numeric_limits<double>::min(),
            2000.0
        )
    );

    const QString orientation =
        boundedString(profile, "pageOrientation", path, 12);
    if (
        orientation != QStringLiteral("portrait")
        && orientation != QStringLiteral("landscape")
    ) {
        invalid(path + ".pageOrientation is invalid.");
    }

    const QString duplex =
        boundedString(profile, "duplexMode", path, 32);
    static const std::set<QString> Duplex{
        QStringLiteral("manual-long-edge"),
        QStringLiteral("manual-short-edge"),
        QStringLiteral("automatic-long-edge"),
        QStringLiteral("automatic-short-edge"),
        QStringLiteral("single-sided"),
    };
    if (!Duplex.contains(duplex)) {
        invalid(path + ".duplexMode is invalid.");
    }

    for (const auto& [key, maximum] :
         std::array<std::pair<const char*, int>, 4>{
             std::pair{"mediaType", 160},
             std::pair{"printQualityProfile", 160},
             std::pair{"feedSource", 160},
             std::pair{"notes", 2000},
         }) {
        if (profile.contains(QLatin1String(key))) {
            static_cast<void>(
                boundedString(profile, key, path, maximum, true)
            );
        }
    }

    const QString validationStatus =
        boundedString(profile, "physicalValidationStatus", path, 32);
    if (
        validationStatus != QStringLiteral("software-only")
        && validationStatus != QStringLiteral("physically-verified")
    ) {
        invalid(path + ".physicalValidationStatus is invalid.");
    }

    const QJsonValue verification =
        profile.value(QStringLiteral("physicalVerification"));
    const bool hasVerification = verification.isObject();
    const bool noVerification =
        verification.isUndefined() || verification.isNull();

    if (!noVerification && !hasVerification) {
        invalid(path + ".physicalVerification must be null or an object.");
    }

    if (hasVerification) {
        validatePhysicalVerification(
            verification.toObject(),
            path + ".physicalVerification"
        );
    }

    if (
        (validationStatus == QStringLiteral("physically-verified"))
        != hasVerification
    ) {
        invalid(path + " verification status does not match evidence.");
    }

    const QJsonValue version = profile.value(QStringLiteral("version"));
    if (
        !version.isDouble()
        || std::floor(version.toDouble()) != version.toDouble()
        || version.toDouble() < 1
        || version.toDouble() > 9007199254740991.0
    ) {
        invalid(path + ".version must be a positive safe integer.");
    }

    const QString hash =
        boundedString(profile, "profileHash", path, 64);
    static const QRegularExpression Sha(
        QStringLiteral("^[a-f0-9]{64}$")
    );
    if (!Sha.match(hash).hasMatch()) {
        invalid(path + ".profileHash must be a lowercase SHA-256 digest.");
    }
}

} // namespace

void validateProjectSettingsReferences(
    const QJsonObject& settings
)
{
    const QJsonValue projectDefaultBack =
        settings.value(QStringLiteral("projectDefaultBack"));
    if (!projectDefaultBack.isNull()) {
        if (!projectDefaultBack.isObject()) {
            invalid("snapshot.settings.projectDefaultBack must be null or an object.");
        }
        validateBackReference(
            projectDefaultBack.toObject(),
            "snapshot.settings.projectDefaultBack"
        );
    }

    const QJsonValue cutSource =
        settings.value(QStringLiteral("cutSourceSelection"));
    if (!cutSource.isNull()) {
        if (!cutSource.isObject()) {
            invalid("snapshot.settings.cutSourceSelection must be null or an object.");
        }
        validateCutSourceSelection(cutSource.toObject());
    }

    const QJsonValue printerProfile =
        settings.value(QStringLiteral("printerProfileSelection"));
    if (!printerProfile.isNull()) {
        if (!printerProfile.isObject()) {
            invalid("snapshot.settings.printerProfileSelection must be null or an object.");
        }
        validatePrinterProfileSnapshot(
            printerProfile.toObject()
        );
    }
}

} // namespace tcgprint::projects
