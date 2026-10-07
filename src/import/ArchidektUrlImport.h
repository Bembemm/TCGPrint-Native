#pragma once

#include "import/ImportTypes.h"
#include "import/UrlHttpTransport.h"

#include <QString>

namespace tcgprint::imports {

struct ArchidektUrlImportResult {
    ImportSource source;
    ImporterOutput output;
};

ArchidektUrlImportResult importArchidektDeckUrl(
    const QString& value,
    const QString& sourceId,
    int order = 0,
    const UrlFetchOptions& fetchOptions = {}
);

} // namespace tcgprint::imports
