#include "domain/projects/ProjectSettings.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace tcgprint::projects {
namespace {

bool finiteBetween(
    geometry::Millimeters value,
    double minimum,
    double maximum
)
{
    const double number = value.value();
    return std::isfinite(number)
        && number >= minimum
        && number <= maximum;
}

} // namespace

PaperFormat PaperFormat::a4()
{
    return PaperFormat{
        .name = "A4",
        .width = geometry::Millimeters(210.0),
        .height = geometry::Millimeters(297.0),
    };
}

ProjectPrintSettings ProjectPrintSettings::defaults()
{
    return ProjectPrintSettings{
        .bleed = geometry::Millimeters(0.625),
        .roundedCorners = false,
        .cutGuides = CutGuideSettings{
            .trim = TrimGuideSettings{
                .enabled = false,
                .fullExtent = false,
                .extent = geometry::Millimeters(1.0),
                .color = GuideColor::Blue,
            },
            .external = ExternalGuideSettings{
                .enabled = false,
                .strokeWidthPt = 0.3,
                .color = GuideColor::Black,
            },
        },
        .pageOrientation = PageOrientation::Portrait,
        .cardOrientation = PageOrientation::Portrait,
        .paperFormat = PaperFormat::a4(),
        .cardFormat = geometry::CardFormat::magicStandard(),
        .margins = Margins{
            .top = geometry::Millimeters(0.0),
            .right = geometry::Millimeters(0.0),
            .bottom = geometry::Millimeters(0.0),
            .left = geometry::Millimeters(0.0),
        },
        .horizontalGap = geometry::Millimeters(0.0),
        .verticalGap = geometry::Millimeters(0.0),
        .exportContentMode = ExportContentMode::FrontOnly,
        .missingBackPolicy = MissingBackPolicy::UseProjectDefault,
        .duplexFlipMode = DuplexFlipMode::LongEdge,
        .printerDuplexMode = PrinterDuplexMode::SingleSided,
        .layout = LayoutSettings{},
    };
}

ProjectSettingsError::ProjectSettingsError(
    ProjectSettingsErrorCode code,
    std::string message
)
    : std::runtime_error(std::move(message)),
      code_(code)
{
}

ProjectSettingsErrorCode ProjectSettingsError::code() const noexcept
{
    return code_;
}

void validateProjectPrintSettings(
    const ProjectPrintSettings& settings
)
{
    if (!finiteBetween(settings.bleed, 0.0, 3.0)) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidBleed,
            "Bleed must be finite and between 0 and 3 mm."
        );
    }

    if (
        (!settings.cutGuides.trim.fullExtent
         && !finiteBetween(
             settings.cutGuides.trim.extent,
             std::numeric_limits<double>::min(),
             2000.0
         ))
        || !std::isfinite(settings.cutGuides.external.strokeWidthPt)
        || settings.cutGuides.external.strokeWidthPt <= 0.0
        || settings.cutGuides.external.strokeWidthPt > 100.0
    ) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidCutGuides,
            "Printed cut guide geometry is invalid."
        );
    }

    if (
        settings.paperFormat.name.empty()
        || !finiteBetween(
            settings.paperFormat.width,
            std::numeric_limits<double>::min(),
            2000.0
        )
        || !finiteBetween(
            settings.paperFormat.height,
            std::numeric_limits<double>::min(),
            2000.0
        )
    ) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidPaperFormat,
            "Paper format dimensions must be positive and no greater than 2000 mm."
        );
    }

    if (
        !settings.cardFormat.isValid()
        || !finiteBetween(
            settings.cardFormat.trimWidth,
            std::numeric_limits<double>::min(),
            2000.0
        )
        || !finiteBetween(
            settings.cardFormat.trimHeight,
            std::numeric_limits<double>::min(),
            2000.0
        )
        || !finiteBetween(
            settings.cardFormat.cornerRadius,
            0.0,
            1000.0
        )
        || settings.cardFormat.cornerRadius.value() * 2.0
            > std::min(
                settings.cardFormat.trimWidth.value(),
                settings.cardFormat.trimHeight.value()
            )
    ) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidCardFormat,
            "Card format dimensions or corner radius are invalid."
        );
    }

    for (const geometry::Millimeters margin : {
        settings.margins.top,
        settings.margins.right,
        settings.margins.bottom,
        settings.margins.left,
    }) {
        if (!finiteBetween(margin, 0.0, 2000.0)) {
            throw ProjectSettingsError(
                ProjectSettingsErrorCode::InvalidMargins,
                "Margins must be finite and between 0 and 2000 mm."
            );
        }
    }

    if (
        !finiteBetween(settings.horizontalGap, 0.0, 2000.0)
        || !finiteBetween(settings.verticalGap, 0.0, 2000.0)
    ) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidGap,
            "Card gaps must be finite and between 0 and 2000 mm."
        );
    }

    const bool hasRows = settings.layout.rows.has_value();
    const bool hasColumns = settings.layout.columns.has_value();

    if (hasRows != hasColumns) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidLayout,
            "Rows and columns must be supplied together."
        );
    }

    std::optional<std::size_t> fixedSlots;
    if (hasRows && hasColumns) {
        if (
            *settings.layout.rows < 1
            || *settings.layout.columns < 1
            || *settings.layout.rows > 1128
            || *settings.layout.columns > 1128
            || *settings.layout.rows
                > 1128 / *settings.layout.columns
        ) {
            throw ProjectSettingsError(
                ProjectSettingsErrorCode::InvalidLayout,
                "Fixed grid must contain between 1 and 1128 positions."
            );
        }

        fixedSlots =
            *settings.layout.rows * *settings.layout.columns;
    }

    if (settings.layout.skippedSlotIndices.size() > 1128) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidLayout,
            "Skipped slot list exceeds 1128 positions."
        );
    }

    std::set<std::size_t> skipped;
    for (const std::size_t index :
         settings.layout.skippedSlotIndices) {
        if (!skipped.insert(index).second) {
            throw ProjectSettingsError(
                ProjectSettingsErrorCode::InvalidLayout,
                "Skipped slot indices must be unique."
            );
        }

        if (fixedSlots && index >= *fixedSlots) {
            throw ProjectSettingsError(
                ProjectSettingsErrorCode::InvalidLayout,
                "Skipped slot index lies outside the fixed grid."
            );
        }
    }

    if (
        !settings.layout.skippedSlotIndices.empty()
        && !fixedSlots
        && !settings.layout.hasTemplateGeometry
    ) {
        throw ProjectSettingsError(
            ProjectSettingsErrorCode::InvalidLayout,
            "Skipped slots require a fixed grid or template geometry."
        );
    }
}

} // namespace tcgprint::projects
