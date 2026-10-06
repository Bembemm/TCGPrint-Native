#pragma once

#include "import/ImportLimits.h"
#include "import/ImportTypes.h"

#include <optional>

namespace tcgprint::imports {

ImporterOutput parseJsonImport(
    const ImportSource& source,
    const std::optional<JsonImportMapping>& mapping = std::nullopt,
    const ImportLimitOverrides& limitOverrides = {}
);

} // namespace tcgprint::imports
