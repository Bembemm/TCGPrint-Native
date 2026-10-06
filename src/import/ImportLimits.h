#pragma once

#include <cstdint>
#include <optional>

namespace tcgprint::imports {

struct ImportLimits {
    std::uint64_t maxInputBytes;
    std::uint64_t maxRasterPixels;
    std::uint64_t maxSvgBytes;
    std::uint64_t maxTextBytes;
    std::uint64_t maxCsvBytes;
    std::uint64_t maxCsvRows;
    std::uint64_t maxJsonBytes;
    std::uint64_t maxJsonDepth;
    std::uint64_t maxJsonNodes;
    std::uint64_t maxXmlBytes;
    std::uint64_t maxXmlDepth;
    std::uint64_t maxXmlNodes;
    std::uint64_t maxZipArchiveBytes;
    std::uint64_t maxZipEntries;
    std::uint64_t maxZipEntryBytes;
    std::uint64_t maxZipTotalUncompressedBytes;
    std::uint64_t maxZipCompressionRatio;
    std::uint64_t maxZipNestingDepth;
};

struct ImportLimitOverrides {
    std::optional<std::uint64_t> maxInputBytes;
    std::optional<std::uint64_t> maxRasterPixels;
    std::optional<std::uint64_t> maxSvgBytes;
    std::optional<std::uint64_t> maxTextBytes;
    std::optional<std::uint64_t> maxCsvBytes;
    std::optional<std::uint64_t> maxCsvRows;
    std::optional<std::uint64_t> maxJsonBytes;
    std::optional<std::uint64_t> maxJsonDepth;
    std::optional<std::uint64_t> maxJsonNodes;
    std::optional<std::uint64_t> maxXmlBytes;
    std::optional<std::uint64_t> maxXmlDepth;
    std::optional<std::uint64_t> maxXmlNodes;
    std::optional<std::uint64_t> maxZipArchiveBytes;
    std::optional<std::uint64_t> maxZipEntries;
    std::optional<std::uint64_t> maxZipEntryBytes;
    std::optional<std::uint64_t> maxZipTotalUncompressedBytes;
    std::optional<std::uint64_t> maxZipCompressionRatio;
    std::optional<std::uint64_t> maxZipNestingDepth;
};

inline constexpr ImportLimits ImportLimitsDefault{
    .maxInputBytes = 50ULL * 1024ULL * 1024ULL,
    .maxRasterPixels = 100'000'000ULL,
    .maxSvgBytes = 5ULL * 1024ULL * 1024ULL,
    .maxTextBytes = 5ULL * 1024ULL * 1024ULL,
    .maxCsvBytes = 25ULL * 1024ULL * 1024ULL,
    .maxCsvRows = 100'000ULL,
    .maxJsonBytes = 8ULL * 1024ULL * 1024ULL,
    .maxJsonDepth = 64ULL,
    .maxJsonNodes = 250'000ULL,
    .maxXmlBytes = 5ULL * 1024ULL * 1024ULL,
    .maxXmlDepth = 64ULL,
    .maxXmlNodes = 100'000ULL,
    .maxZipArchiveBytes = 100ULL * 1024ULL * 1024ULL,
    .maxZipEntries = 500ULL,
    .maxZipEntryBytes = 50ULL * 1024ULL * 1024ULL,
    .maxZipTotalUncompressedBytes = 200ULL * 1024ULL * 1024ULL,
    .maxZipCompressionRatio = 100ULL,
    .maxZipNestingDepth = 3ULL,
};

ImportLimits resolveImportLimits(
    const ImportLimitOverrides& overrides = {}
);

} // namespace tcgprint::imports
