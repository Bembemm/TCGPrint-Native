#pragma once

#include "domain/cards/WorkingCard.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcgprint::cards {

inline constexpr std::size_t MaxPhysicalCardsPerExport = 500;

using PhysicalInstanceId = std::uint64_t;
inline constexpr PhysicalInstanceId MaxPersistedPhysicalInstanceId = 9007199254740991ULL;

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

enum class PhysicalOrderErrorCode
{
    InvalidPhysicalOrder,
    InvalidPhysicalInstanceId,
    PhysicalInstanceNotFound,
    ExportLimitExceeded,
};

class PhysicalOrderError final : public std::runtime_error
{
public:
    PhysicalOrderError(PhysicalOrderErrorCode code, std::string message);

    [[nodiscard]] PhysicalOrderErrorCode code() const noexcept;

private:
    PhysicalOrderErrorCode code_;
};

struct AddPhysicalInstanceResult final
{
    PhysicalOrder order;
    PhysicalInstanceRef instance;
};

struct RemovePhysicalInstanceResult final
{
    PhysicalOrder order;
    PhysicalInstanceRef removed;
};

[[nodiscard]] std::string physicalInstanceIdString(PhysicalInstanceId id);

[[nodiscard]] std::optional<PhysicalInstanceId> parsePhysicalInstanceIdString(
    const std::string& id
) noexcept;

[[nodiscard]] PhysicalOrder makeLegacyPhysicalOrder(
    const std::vector<WorkingCard>& cards
);

[[nodiscard]] PhysicalOrder validatePhysicalOrder(
    const std::vector<WorkingCard>& cards,
    const PhysicalOrder& order
);

[[nodiscard]] PhysicalOrder reconcilePhysicalOrder(
    const std::vector<WorkingCard>& cards,
    const std::optional<PhysicalOrder>& previous = std::nullopt
);

[[nodiscard]] std::optional<std::size_t> physicalInstanceIndex(
    const PhysicalOrder& order,
    PhysicalInstanceId id
);

[[nodiscard]] AddPhysicalInstanceResult addPhysicalInstance(
    const PhysicalOrder& order,
    const std::string& workingCardId,
    std::optional<std::size_t> afterIndex = std::nullopt
);

[[nodiscard]] RemovePhysicalInstanceResult removePhysicalInstance(
    const PhysicalOrder& order,
    PhysicalInstanceId instanceId
);

[[nodiscard]] PhysicalOrder replacePhysicalInstanceCard(
    const PhysicalOrder& order,
    PhysicalInstanceId instanceId,
    std::string workingCardId
);

[[nodiscard]] PhysicalOrder movePhysicalInstance(
    const PhysicalOrder& order,
    PhysicalInstanceId sourceId,
    std::optional<PhysicalInstanceId> targetId,
    InsertPosition position = InsertPosition::After
);

} // namespace tcgprint::cards
