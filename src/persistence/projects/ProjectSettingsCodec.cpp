#include "persistence/projects/ProjectSettingsCodec.h"

#include "persistence/projects/ProjectSnapshot.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <string>

namespace tcgprint::projects {
namespace {

[[noreturn]] void invalid(const std::string& message)
{
    throw ProjectSnapshotError(
        ProjectSnapshotErrorCode::InvalidProjectSnapshot,
        message
    );
}

double requiredNumber(
    const QJsonObject& object,
    const char* key,
    const std::string& path
)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        invalid(path + "." + key + " must be a finite number.");
    }
    return value.toDouble();
}

QString requiredString(
    const QJsonObject& object,
    const char* key,
    const std::string& path
)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        invalid(path + "." + key + " must be a non-empty string.");
    }
    return value.toString();
}

PageOrientation orientationFromString(
    const QString& value,
    const std::string& path
)
{
    if (value == QStringLiteral("portrait")) {
        return PageOrientation::Portrait;
    }
    if (value == QStringLiteral("landscape")) {
        return PageOrientation::Landscape;
    }
    invalid(path + " must be portrait or landscape.");
}

QString orientationString(PageOrientation value)
{
    return value == PageOrientation::Landscape
        ? QStringLiteral("landscape")
        : QStringLiteral("portrait");
}

GuideColor guideColorFromString(
    const QString& value,
    const std::string& path
)
{
    if (value == QStringLiteral("black")) return GuideColor::Black;
    if (value == QStringLiteral("white")) return GuideColor::White;
    if (value == QStringLiteral("red")) return GuideColor::Red;
    if (value == QStringLiteral("green")) return GuideColor::Green;
    if (value == QStringLiteral("blue")) return GuideColor::Blue;
    invalid(path + " is not a supported guide color.");
}

QString guideColorString(GuideColor value)
{
    switch (value) {
    case GuideColor::Black: return QStringLiteral("black");
    case GuideColor::White: return QStringLiteral("white");
    case GuideColor::Red: return QStringLiteral("red");
    case GuideColor::Green: return QStringLiteral("green");
    case GuideColor::Blue: return QStringLiteral("blue");
    }
    return QStringLiteral("black");
}

ExportContentMode exportContentModeFromString(const QString& value)
{
    if (value == QStringLiteral("front-only")) {
        return ExportContentMode::FrontOnly;
    }
    if (value == QStringLiteral("back-only")) {
        return ExportContentMode::BackOnly;
    }
    if (value == QStringLiteral("front-back-separated")) {
        return ExportContentMode::FrontBackSeparated;
    }
    if (value == QStringLiteral("duplex")) {
        return ExportContentMode::Duplex;
    }
    invalid("snapshot.settings.exportContentMode is invalid.");
}

QString exportContentModeString(ExportContentMode value)
{
    switch (value) {
    case ExportContentMode::FrontOnly:
        return QStringLiteral("front-only");
    case ExportContentMode::BackOnly:
        return QStringLiteral("back-only");
    case ExportContentMode::FrontBackSeparated:
        return QStringLiteral("front-back-separated");
    case ExportContentMode::Duplex:
        return QStringLiteral("duplex");
    }
    return QStringLiteral("front-only");
}

MissingBackPolicy missingBackPolicyFromString(const QString& value)
{
    if (value == QStringLiteral("use-project-default")) {
        return MissingBackPolicy::UseProjectDefault;
    }
    if (value == QStringLiteral("blank")) {
        return MissingBackPolicy::Blank;
    }
    if (value == QStringLiteral("warn-and-continue")) {
        return MissingBackPolicy::WarnAndContinue;
    }
    if (value == QStringLiteral("block")) {
        return MissingBackPolicy::Block;
    }
    invalid("snapshot.settings.missingBackPolicy is invalid.");
}

QString missingBackPolicyString(MissingBackPolicy value)
{
    switch (value) {
    case MissingBackPolicy::UseProjectDefault:
        return QStringLiteral("use-project-default");
    case MissingBackPolicy::Blank:
        return QStringLiteral("blank");
    case MissingBackPolicy::WarnAndContinue:
        return QStringLiteral("warn-and-continue");
    case MissingBackPolicy::Block:
        return QStringLiteral("block");
    }
    return QStringLiteral("use-project-default");
}

DuplexFlipMode duplexFlipModeFromString(const QString& value)
{
    if (value == QStringLiteral("long-edge")) {
        return DuplexFlipMode::LongEdge;
    }
    if (value == QStringLiteral("short-edge")) {
        return DuplexFlipMode::ShortEdge;
    }
    invalid("snapshot.settings.duplexFlipMode is invalid.");
}

QString duplexFlipModeString(DuplexFlipMode value)
{
    return value == DuplexFlipMode::ShortEdge
        ? QStringLiteral("short-edge")
        : QStringLiteral("long-edge");
}

PrinterDuplexMode printerDuplexModeFromString(const QString& value)
{
    if (value == QStringLiteral("manual-long-edge")) {
        return PrinterDuplexMode::ManualLongEdge;
    }
    if (value == QStringLiteral("manual-short-edge")) {
        return PrinterDuplexMode::ManualShortEdge;
    }
    if (value == QStringLiteral("automatic-long-edge")) {
        return PrinterDuplexMode::AutomaticLongEdge;
    }
    if (value == QStringLiteral("automatic-short-edge")) {
        return PrinterDuplexMode::AutomaticShortEdge;
    }
    if (value == QStringLiteral("single-sided")) {
        return PrinterDuplexMode::SingleSided;
    }
    invalid("snapshot.settings.printerDuplexMode is invalid.");
}

QString printerDuplexModeString(PrinterDuplexMode value)
{
    switch (value) {
    case PrinterDuplexMode::ManualLongEdge:
        return QStringLiteral("manual-long-edge");
    case PrinterDuplexMode::ManualShortEdge:
        return QStringLiteral("manual-short-edge");
    case PrinterDuplexMode::AutomaticLongEdge:
        return QStringLiteral("automatic-long-edge");
    case PrinterDuplexMode::AutomaticShortEdge:
        return QStringLiteral("automatic-short-edge");
    case PrinterDuplexMode::SingleSided:
        return QStringLiteral("single-sided");
    }
    return QStringLiteral("single-sided");
}

std::optional<std::size_t> optionalPositiveInteger(
    const QJsonObject& object,
    const char* key,
    const std::string& path
)
{
    if (!object.contains(QLatin1String(key))) {
        return std::nullopt;
    }

    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble()) {
        invalid(path + "." + key + " must be a positive integer.");
    }

    const double number = value.toDouble();
    if (
        !std::isfinite(number)
        || std::floor(number) != number
        || number < 1
        || number > 1128
    ) {
        invalid(path + "." + key + " must be a positive integer.");
    }

    return static_cast<std::size_t>(number);
}

} // namespace

ProjectPrintSettings parseProjectPrintSettings(
    const QJsonObject& settings
)
{
    ProjectPrintSettings result = ProjectPrintSettings::defaults();

    result.bleed = geometry::Millimeters(
        requiredNumber(
            settings,
            "bleedMm",
            "snapshot.settings"
        )
    );

    const QJsonValue rounded =
        settings.value(QStringLiteral("roundedCorners"));
    if (!rounded.isBool()) {
        invalid("snapshot.settings.roundedCorners must be a boolean.");
    }
    result.roundedCorners = rounded.toBool();

    const QJsonObject cutGuides =
        settings.value(QStringLiteral("cutGuides")).toObject();
    const QJsonObject trim =
        cutGuides.value(QStringLiteral("trim")).toObject();
    const QJsonObject external =
        cutGuides.value(QStringLiteral("external")).toObject();

    if (
        !cutGuides.contains(QStringLiteral("trim"))
        || !cutGuides.contains(QStringLiteral("external"))
        || !trim.value(QStringLiteral("enabled")).isBool()
        || !external.value(QStringLiteral("enabled")).isBool()
    ) {
        invalid("snapshot.settings.cutGuides is incomplete.");
    }

    result.cutGuides.trim.enabled =
        trim.value(QStringLiteral("enabled")).toBool();

    const QJsonValue extent =
        trim.value(QStringLiteral("extentMm"));
    if (extent.isString()) {
        if (extent.toString() != QStringLiteral("full")) {
            invalid("snapshot.settings.cutGuides.trim.extentMm is invalid.");
        }
        result.cutGuides.trim.fullExtent = true;
    } else if (extent.isDouble()) {
        result.cutGuides.trim.fullExtent = false;
        result.cutGuides.trim.extent =
            geometry::Millimeters(extent.toDouble());
    } else {
        invalid("snapshot.settings.cutGuides.trim.extentMm is invalid.");
    }

    result.cutGuides.trim.color = guideColorFromString(
        requiredString(
            trim,
            "color",
            "snapshot.settings.cutGuides.trim"
        ),
        "snapshot.settings.cutGuides.trim.color"
    );

    result.cutGuides.external.enabled =
        external.value(QStringLiteral("enabled")).toBool();
    result.cutGuides.external.strokeWidthPt =
        requiredNumber(
            external,
            "strokeWidthPt",
            "snapshot.settings.cutGuides.external"
        );
    result.cutGuides.external.color = guideColorFromString(
        requiredString(
            external,
            "color",
            "snapshot.settings.cutGuides.external"
        ),
        "snapshot.settings.cutGuides.external.color"
    );

    result.pageOrientation = orientationFromString(
        requiredString(
            settings,
            "pageOrientation",
            "snapshot.settings"
        ),
        "snapshot.settings.pageOrientation"
    );
    result.cardOrientation = orientationFromString(
        requiredString(
            settings,
            "cardOrientation",
            "snapshot.settings"
        ),
        "snapshot.settings.cardOrientation"
    );

    const QJsonObject paper =
        settings.value(QStringLiteral("paperFormat")).toObject();
    result.paperFormat = PaperFormat{
        .name = requiredString(
            paper,
            "name",
            "snapshot.settings.paperFormat"
        ).toStdString(),
        .width = geometry::Millimeters(
            requiredNumber(
                paper,
                "widthMm",
                "snapshot.settings.paperFormat"
            )
        ),
        .height = geometry::Millimeters(
            requiredNumber(
                paper,
                "heightMm",
                "snapshot.settings.paperFormat"
            )
        ),
    };

    const QJsonObject card =
        settings.value(QStringLiteral("cardFormat")).toObject();
    result.cardFormat = geometry::CardFormat{
        .id = requiredString(
            card,
            "id",
            "snapshot.settings.cardFormat"
        ).toStdString(),
        .name = requiredString(
            card,
            "name",
            "snapshot.settings.cardFormat"
        ).toStdString(),
        .trimWidth = geometry::Millimeters(
            requiredNumber(
                card,
                "widthMm",
                "snapshot.settings.cardFormat"
            )
        ),
        .trimHeight = geometry::Millimeters(
            requiredNumber(
                card,
                "heightMm",
                "snapshot.settings.cardFormat"
            )
        ),
        .cornerRadius = geometry::Millimeters(
            card.contains(QStringLiteral("cornerRadiusMm"))
                ? requiredNumber(
                    card,
                    "cornerRadiusMm",
                    "snapshot.settings.cardFormat"
                )
                : 0.0
        ),
    };

    const QJsonObject margins =
        settings.value(QStringLiteral("marginsMm")).toObject();
    result.margins = Margins{
        .top = geometry::Millimeters(
            requiredNumber(margins, "top", "snapshot.settings.marginsMm")
        ),
        .right = geometry::Millimeters(
            requiredNumber(margins, "right", "snapshot.settings.marginsMm")
        ),
        .bottom = geometry::Millimeters(
            requiredNumber(margins, "bottom", "snapshot.settings.marginsMm")
        ),
        .left = geometry::Millimeters(
            requiredNumber(margins, "left", "snapshot.settings.marginsMm")
        ),
    };

    result.horizontalGap = geometry::Millimeters(
        requiredNumber(
            settings,
            "horizontalGapMm",
            "snapshot.settings"
        )
    );
    result.verticalGap = geometry::Millimeters(
        requiredNumber(
            settings,
            "verticalGapMm",
            "snapshot.settings"
        )
    );

    result.exportContentMode = exportContentModeFromString(
        requiredString(
            settings,
            "exportContentMode",
            "snapshot.settings"
        )
    );
    result.missingBackPolicy = missingBackPolicyFromString(
        requiredString(
            settings,
            "missingBackPolicy",
            "snapshot.settings"
        )
    );
    result.duplexFlipMode = duplexFlipModeFromString(
        requiredString(
            settings,
            "duplexFlipMode",
            "snapshot.settings"
        )
    );
    result.printerDuplexMode = printerDuplexModeFromString(
        requiredString(
            settings,
            "printerDuplexMode",
            "snapshot.settings"
        )
    );

    const QJsonObject layout =
        settings.value(QStringLiteral("layout")).toObject();
    result.layout.rows = optionalPositiveInteger(
        layout,
        "rows",
        "snapshot.settings.layout"
    );
    result.layout.columns = optionalPositiveInteger(
        layout,
        "columns",
        "snapshot.settings.layout"
    );

    const QJsonValue skippedValue =
        layout.value(QStringLiteral("skippedSlotIndices"));
    if (!skippedValue.isArray()) {
        invalid("snapshot.settings.layout.skippedSlotIndices must be an array.");
    }

    const QJsonArray skipped = skippedValue.toArray();
    result.layout.skippedSlotIndices.reserve(
        static_cast<std::size_t>(skipped.size())
    );

    for (qsizetype index = 0; index < skipped.size(); ++index) {
        const QJsonValue value = skipped.at(index);
        if (!value.isDouble()) {
            invalid("snapshot.settings.layout.skippedSlotIndices contains a non-integer.");
        }
        const double number = value.toDouble();
        if (
            !std::isfinite(number)
            || std::floor(number) != number
            || number < 0
        ) {
            invalid("snapshot.settings.layout.skippedSlotIndices contains an invalid index.");
        }
        result.layout.skippedSlotIndices.push_back(
            static_cast<std::size_t>(number)
        );
    }

    result.layout.hasTemplateGeometry =
        layout.value(QStringLiteral("templateGeometry")).isObject();

    try {
        validateProjectPrintSettings(result);
    } catch (const ProjectSettingsError& error) {
        invalid(error.what());
    }

    return result;
}

QJsonObject overlayProjectPrintSettings(
    const QJsonObject& compatibleSettings,
    const ProjectPrintSettings& printSettings
)
{
    validateProjectPrintSettings(printSettings);

    QJsonObject result = compatibleSettings;

    result.insert(
        QStringLiteral("bleedMm"),
        printSettings.bleed.value()
    );
    result.insert(
        QStringLiteral("roundedCorners"),
        printSettings.roundedCorners
    );

    QJsonObject trim;
    trim.insert(
        QStringLiteral("enabled"),
        printSettings.cutGuides.trim.enabled
    );
    trim.insert(
        QStringLiteral("extentMm"),
        printSettings.cutGuides.trim.fullExtent
            ? QJsonValue(QStringLiteral("full"))
            : QJsonValue(printSettings.cutGuides.trim.extent.value())
    );
    trim.insert(
        QStringLiteral("color"),
        guideColorString(printSettings.cutGuides.trim.color)
    );

    QJsonObject external;
    external.insert(
        QStringLiteral("enabled"),
        printSettings.cutGuides.external.enabled
    );
    external.insert(
        QStringLiteral("strokeWidthPt"),
        printSettings.cutGuides.external.strokeWidthPt
    );
    external.insert(
        QStringLiteral("color"),
        guideColorString(printSettings.cutGuides.external.color)
    );

    QJsonObject guides;
    guides.insert(QStringLiteral("trim"), trim);
    guides.insert(QStringLiteral("external"), external);
    result.insert(QStringLiteral("cutGuides"), guides);

    result.insert(
        QStringLiteral("pageOrientation"),
        orientationString(printSettings.pageOrientation)
    );
    result.insert(
        QStringLiteral("cardOrientation"),
        orientationString(printSettings.cardOrientation)
    );

    QJsonObject paper;
    paper.insert(
        QStringLiteral("name"),
        QString::fromStdString(printSettings.paperFormat.name)
    );
    paper.insert(
        QStringLiteral("widthMm"),
        printSettings.paperFormat.width.value()
    );
    paper.insert(
        QStringLiteral("heightMm"),
        printSettings.paperFormat.height.value()
    );
    result.insert(QStringLiteral("paperFormat"), paper);

    QJsonObject card;
    card.insert(
        QStringLiteral("id"),
        QString::fromStdString(printSettings.cardFormat.id)
    );
    card.insert(
        QStringLiteral("name"),
        QString::fromStdString(printSettings.cardFormat.name)
    );
    card.insert(
        QStringLiteral("widthMm"),
        printSettings.cardFormat.trimWidth.value()
    );
    card.insert(
        QStringLiteral("heightMm"),
        printSettings.cardFormat.trimHeight.value()
    );
    if (printSettings.cardFormat.cornerRadius.value() > 0.0) {
        card.insert(
            QStringLiteral("cornerRadiusMm"),
            printSettings.cardFormat.cornerRadius.value()
        );
    }
    result.insert(QStringLiteral("cardFormat"), card);

    QJsonObject margins;
    margins.insert(
        QStringLiteral("top"),
        printSettings.margins.top.value()
    );
    margins.insert(
        QStringLiteral("right"),
        printSettings.margins.right.value()
    );
    margins.insert(
        QStringLiteral("bottom"),
        printSettings.margins.bottom.value()
    );
    margins.insert(
        QStringLiteral("left"),
        printSettings.margins.left.value()
    );
    result.insert(QStringLiteral("marginsMm"), margins);

    result.insert(
        QStringLiteral("horizontalGapMm"),
        printSettings.horizontalGap.value()
    );
    result.insert(
        QStringLiteral("verticalGapMm"),
        printSettings.verticalGap.value()
    );
    result.insert(
        QStringLiteral("exportContentMode"),
        exportContentModeString(printSettings.exportContentMode)
    );
    result.insert(
        QStringLiteral("missingBackPolicy"),
        missingBackPolicyString(printSettings.missingBackPolicy)
    );
    result.insert(
        QStringLiteral("duplexFlipMode"),
        duplexFlipModeString(printSettings.duplexFlipMode)
    );
    result.insert(
        QStringLiteral("printerDuplexMode"),
        printerDuplexModeString(printSettings.printerDuplexMode)
    );

    QJsonObject layout =
        result.value(QStringLiteral("layout")).toObject();

    if (printSettings.layout.rows && printSettings.layout.columns) {
        layout.insert(
            QStringLiteral("rows"),
            static_cast<double>(*printSettings.layout.rows)
        );
        layout.insert(
            QStringLiteral("columns"),
            static_cast<double>(*printSettings.layout.columns)
        );
    } else {
        layout.remove(QStringLiteral("rows"));
        layout.remove(QStringLiteral("columns"));
    }

    QJsonArray skipped;
    for (const std::size_t index :
         printSettings.layout.skippedSlotIndices) {
        skipped.append(static_cast<double>(index));
    }
    layout.insert(QStringLiteral("skippedSlotIndices"), skipped);

    if (!printSettings.layout.hasTemplateGeometry) {
        layout.remove(QStringLiteral("templateGeometry"));
    }

    result.insert(QStringLiteral("layout"), layout);

    return result;
}

} // namespace tcgprint::projects
