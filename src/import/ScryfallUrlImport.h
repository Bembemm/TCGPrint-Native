#pragma once

#include "import/ImportTypes.h"

#include <QString>

namespace tcgprint::imports {

struct ScryfallUrlImportResult {
    ImportSource source;
    ImporterOutput output;
};

ScryfallUrlImportResult importScryfallCardUrl(
    const QString& value,
    const QString& sourceId,
    int order = 0
);

} // namespace tcgprint::imports
