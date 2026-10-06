#pragma once

#include "domain/geometry/CardFormat.h"
#include "domain/geometry/Millimeters.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcgprint::projects {

enum class PageOrientation
{
    Portrait,
    Landscape,
};

struct PaperFormat final
{
    std::string name;
    geometry::Millimeters width;
    geometry::Millimeters height;

    bool operator==(const PaperFormat&) const = default;

    [[nodiscard]] static PaperFormat a4();
};

struct Margins final
{
    geometry::Millimeters top;
    geometry::Millimeters right;
    geometry::Millimeters bottom;
    geometry::Millimeters left;

    bool operator==(const Margins&) const = default;
};

enum class ExportContentMode
{
    FrontOnly,
    BackOnly,
    FrontBackSeparated,
    Duplex,
};

enum class MissingBackPolicy
{
    UseProjectDefault,
    Blank,
    WarnAndContinue,
    Block,
};

enum class DuplexFlipMode
{
    LongEdge,
    ShortEdge,
};

enum class PrinterDuplexMode
{
    ManualLongEdge,
    ManualShortEdge,
    AutomaticLongEdge,
    AutomaticShortEdge,
    SingleSided,
};

struct LayoutSettings final
{
    std::optional<std::size_t> rows;
    std::optional<std::size_t> columns;
    std::vector<std::size_t> skippedSlotIndices;
    bool hasTemplateGeometry{false};

    bool operator==(const LayoutSettings&) const = default;
};

struct ProjectPrintSettings final
{
    geometry::Millimeters bleed;
    bool roundedCorners{false};
    PageOrientation pageOrientation{PageOrientation::Portrait};
    PageOrientation cardOrientation{PageOrientation::Portrait};
    PaperFormat paperFormat;
    geometry::CardFormat cardFormat;
    Margins margins;
    geometry::Millimeters horizontalGap;
    geometry::Millimeters verticalGap;
    ExportContentMode exportContentMode{ExportContentMode::FrontOnly};
    MissingBackPolicy missingBackPolicy{
        MissingBackPolicy::UseProjectDefault
    };
    DuplexFlipMode duplexFlipMode{DuplexFlipMode::LongEdge};
    PrinterDuplexMode printerDuplexMode{
        PrinterDuplexMode::SingleSided
    };
    LayoutSettings layout;

    bool operator==(const ProjectPrintSettings&) const = default;

    [[nodiscard]] static ProjectPrintSettings defaults();
};

enum class ProjectSettingsErrorCode
{
    InvalidBleed,
    InvalidPaperFormat,
    InvalidCardFormat,
    InvalidMargins,
    InvalidGap,
    InvalidLayout,
};

class ProjectSettingsError final : public std::runtime_error
{
public:
    ProjectSettingsError(
        ProjectSettingsErrorCode code,
        std::string message
    );

    [[nodiscard]] ProjectSettingsErrorCode code() const noexcept;

private:
    ProjectSettingsErrorCode code_;
};

void validateProjectPrintSettings(
    const ProjectPrintSettings& settings
);

} // namespace tcgprint::projects
