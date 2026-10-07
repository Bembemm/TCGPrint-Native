#pragma once

#include "import/ImportTypes.h"
#include "import/UrlHttpTransport.h"

#include <QString>

#include <vector>

namespace tcgprint::imports {

struct DirectFileUrlImportResult {
    ImportSource source;
    ImportDetection detection;
    std::vector<ImportWarning> warnings;
};

DirectFileUrlImportResult importDirectFileUrl(
    const QString& value,
    const QString& sourceId,
    int order = 0,
    const UrlFetchOptions& fetchOptions = {}
);

} // namespace tcgprint::imports
