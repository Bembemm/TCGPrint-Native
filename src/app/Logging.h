#pragma once

#include <QString>

namespace tcgprint::logging {

[[nodiscard]] QString initialize();
void shutdown();

} // namespace tcgprint::logging
