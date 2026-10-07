#pragma once

#include "import/ImportLimits.h"
#include "import/ImportTypes.h"

namespace tcgprint::imports {

ImporterOutput importImageSource(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides = {}
);

} // namespace tcgprint::imports
