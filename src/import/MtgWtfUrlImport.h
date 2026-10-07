#pragma once

#include "import/ImportTypes.h"
#include "import/UrlHttpTransport.h"

#include <QString>

namespace tcgprint::imports {

struct MtgWtfUrlImportResult {
    ImportSource source;
    ImporterOutput output;
};

MtgWtfUrlImportResult importMtgWtfUrl(
    const QString& value,
    const QString& sourceId,
    int order = 0,
    const UrlFetchOptions& fetchOptions = {}
);

} // namespace tcgprint::imports
