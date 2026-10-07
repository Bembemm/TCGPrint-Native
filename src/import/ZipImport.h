#pragma once

#include "import/ImportLimits.h"
#include "import/ImportTypes.h"

#include <cstdint>
#include <vector>

namespace tcgprint::imports {

struct ZipExpansionOptions {
    ImportLimitOverrides limits;
    bool expandNestedArchives{true};
};

struct ZipExpansion {
    std::vector<ImportSource> sources;
    std::vector<ImportWarning> errors;
    std::uint64_t entriesSeen{0};
    std::uint64_t uncompressedBytes{0};
};

ZipExpansion expandZipSource(
    const ImportSource& source,
    const ZipExpansionOptions& options = {}
);

} // namespace tcgprint::imports
