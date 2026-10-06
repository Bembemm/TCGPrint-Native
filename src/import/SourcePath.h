#pragma once

#include <QString>

#include <optional>

namespace tcgprint::imports {

std::optional<QString> sanitizeRelativeImportPath(
    const std::optional<QString>& value
);

} // namespace tcgprint::imports
