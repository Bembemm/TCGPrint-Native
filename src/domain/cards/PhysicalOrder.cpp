#include "domain/cards/PhysicalOrder.h"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>

namespace tcgprint::cards {

std::string physicalInstanceIdString(PhysicalInstanceId id)
{
    return "instance-" + std::to_string(id);
}

PhysicalOrder makeLegacyPhysicalOrder(const std::vector<WorkingCard>& cards)
{
    std::vector<std::size_t> indexes(cards.size());
    std::iota(indexes.begin(), indexes.end(), std::size_t{0});

    std::stable_sort(
        indexes.begin(),
        indexes.end(),
        [&cards](std::size_t left, std::size_t right) {
            return cards[left].order < cards[right].order;
        }
    );

    PhysicalOrder result;

    for (const std::size_t index : indexes) {
        const WorkingCard& card = cards[index];

        if (!isValidWorkingCard(card)) {
            continue;
        }

        for (std::uint32_t copy = 0; copy < card.quantity; ++copy) {
            result.instances.push_back(PhysicalInstanceRef{
                .id = result.nextInstanceId++,
                .workingCardId = card.id,
            });
        }
    }

    return result;
}

std::optional<std::size_t> physicalInstanceIndex(
    const PhysicalOrder& order,
    PhysicalInstanceId id
)
{
    const auto iterator = std::find_if(
        order.instances.begin(),
        order.instances.end(),
        [id](const PhysicalInstanceRef& instance) {
            return instance.id == id;
        }
    );

    if (iterator == order.instances.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(
        std::distance(order.instances.begin(), iterator)
    );
}

bool movePhysicalInstance(
    PhysicalOrder& order,
    PhysicalInstanceId sourceId,
    PhysicalInstanceId targetId,
    InsertPosition position
)
{
    if (sourceId == targetId) {
        return false;
    }

    const auto sourceIndex = physicalInstanceIndex(order, sourceId);
    const auto targetIndex = physicalInstanceIndex(order, targetId);

    if (!sourceIndex || !targetIndex) {
        return false;
    }

    PhysicalInstanceRef moving = order.instances[*sourceIndex];
    order.instances.erase(
        order.instances.begin()
        + static_cast<std::ptrdiff_t>(*sourceIndex)
    );

    const auto targetAfterRemoval = physicalInstanceIndex(order, targetId);
    if (!targetAfterRemoval) {
        return false;
    }

    std::size_t insertionIndex = *targetAfterRemoval;
    if (position == InsertPosition::After) {
        ++insertionIndex;
    }

    order.instances.insert(
        order.instances.begin()
            + static_cast<std::ptrdiff_t>(insertionIndex),
        std::move(moving)
    );

    return true;
}

} // namespace tcgprint::cards
