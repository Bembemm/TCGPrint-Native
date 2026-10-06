#pragma once

#include "import/ImportTypes.h"

namespace tcgprint::imports {

ImporterOutput parseTextImport(
    const QString& text,
    ImportKind kind,
    const std::optional<ImportSource>& source = std::nullopt
);

} // namespace tcgprint::imports
