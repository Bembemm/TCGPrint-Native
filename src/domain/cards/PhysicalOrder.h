#pragma once

#include "domain/cards/WorkingCard.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tcgprint::cards {

using PhysicalInstanceId = std::uint64_t;

struct PhysicalInstanceRef final
{
    PhysicalInstanceId id;
    std::string workingCardId;

    bool operator==(const PhysicalInstanceRef&) const = default;
};

struct PhysicalOrder final
{
    PhysicalInstanceId nextInstanceId{1};
    std::vector<PhysicalInstanceRef> instances;

    bool operator==(const PhysicalOrder&) const = default;
};

enum class InsertPosition
{
    Before,
    After,
};

[[nodiscard]] std::string physicalInstanceIdString(PhysicalInstanceId id);

[[nodiscard]] PhysicalOrder makeLegacyPhysicalOrder(
    const std::vector<WorkingCard>& cards
);

[[nodiscard]] std::optional<std::size_t> physicalInstanceIndex(
    const PhysicalOrder& order,
    PhysicalInstanceId id
);

bool movePhysicalInstance(
    PhysicalOrder& order,
    PhysicalInstanceId sourceId,
    PhysicalInstanceId targetId,
    InsertPosition position
);

} // namespace tcgprint::cards
