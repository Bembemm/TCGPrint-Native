#pragma once

#include "import/ImportTypes.h"
#include "import/UrlHttpTransport.h"

#include <QString>

namespace tcgprint::imports {

struct MtgTop8UrlImportResult {
    ImportSource source;
    ImporterOutput output;
};

MtgTop8UrlImportResult importMtgTop8Url(
    const QString& value,
    const QString& sourceId,
    int order = 0,
    const UrlFetchOptions& fetchOptions = {}
);

} // namespace tcgprint::imports
