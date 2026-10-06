#pragma once

#include "import/ImportLimits.h"
#include "import/ImportTypes.h"

#include <optional>

namespace tcgprint::imports {

ImporterOutput parseCsvImport(
    const ImportSource& source,
    const std::optional<CsvImportMapping>& mapping = std::nullopt,
    std::optional<QChar> delimiter = std::nullopt,
    const ImportLimitOverrides& limitOverrides = {}
);

} // namespace tcgprint::imports
