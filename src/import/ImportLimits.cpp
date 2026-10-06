#include "import/ImportLimits.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace tcgprint::imports {
namespace {

std::uint64_t resolved(
    const std::optional<std::uint64_t>& overrideValue,
    std::uint64_t defaultValue,
    std::string_view name
)
{
    const std::uint64_t value =
        overrideValue.value_or(defaultValue);
    if (value == 0) {
        throw std::invalid_argument(
            "Import limit " + std::string(name)
            + " must be a positive safe integer."
        );
    }
    return value;
}

} // namespace

ImportLimits resolveImportLimits(
    const ImportLimitOverrides& overrides
)
{
    return ImportLimits{
        .maxInputBytes = resolved(overrides.maxInputBytes, ImportLimitsDefault.maxInputBytes, "maxInputBytes"),
        .maxRasterPixels = resolved(overrides.maxRasterPixels, ImportLimitsDefault.maxRasterPixels, "maxRasterPixels"),
        .maxSvgBytes = resolved(overrides.maxSvgBytes, ImportLimitsDefault.maxSvgBytes, "maxSvgBytes"),
        .maxTextBytes = resolved(overrides.maxTextBytes, ImportLimitsDefault.maxTextBytes, "maxTextBytes"),
        .maxCsvBytes = resolved(overrides.maxCsvBytes, ImportLimitsDefault.maxCsvBytes, "maxCsvBytes"),
        .maxCsvRows = resolved(overrides.maxCsvRows, ImportLimitsDefault.maxCsvRows, "maxCsvRows"),
        .maxJsonBytes = resolved(overrides.maxJsonBytes, ImportLimitsDefault.maxJsonBytes, "maxJsonBytes"),
        .maxJsonDepth = resolved(overrides.maxJsonDepth, ImportLimitsDefault.maxJsonDepth, "maxJsonDepth"),
        .maxJsonNodes = resolved(overrides.maxJsonNodes, ImportLimitsDefault.maxJsonNodes, "maxJsonNodes"),
        .maxXmlBytes = resolved(overrides.maxXmlBytes, ImportLimitsDefault.maxXmlBytes, "maxXmlBytes"),
        .maxXmlDepth = resolved(overrides.maxXmlDepth, ImportLimitsDefault.maxXmlDepth, "maxXmlDepth"),
        .maxXmlNodes = resolved(overrides.maxXmlNodes, ImportLimitsDefault.maxXmlNodes, "maxXmlNodes"),
        .maxZipArchiveBytes = resolved(overrides.maxZipArchiveBytes, ImportLimitsDefault.maxZipArchiveBytes, "maxZipArchiveBytes"),
        .maxZipEntries = resolved(overrides.maxZipEntries, ImportLimitsDefault.maxZipEntries, "maxZipEntries"),
        .maxZipEntryBytes = resolved(overrides.maxZipEntryBytes, ImportLimitsDefault.maxZipEntryBytes, "maxZipEntryBytes"),
        .maxZipTotalUncompressedBytes = resolved(overrides.maxZipTotalUncompressedBytes, ImportLimitsDefault.maxZipTotalUncompressedBytes, "maxZipTotalUncompressedBytes"),
        .maxZipCompressionRatio = resolved(overrides.maxZipCompressionRatio, ImportLimitsDefault.maxZipCompressionRatio, "maxZipCompressionRatio"),
        .maxZipNestingDepth = resolved(overrides.maxZipNestingDepth, ImportLimitsDefault.maxZipNestingDepth, "maxZipNestingDepth"),
    };
}

} // namespace tcgprint::imports
