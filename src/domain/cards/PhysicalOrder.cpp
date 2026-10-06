#include "domain/cards/PhysicalOrder.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <utility>

namespace tcgprint::cards {
namespace {

using CardIndex = std::size_t;

std::vector<CardIndex> orderedCardIndexes(const std::vector<WorkingCard>& cards)
{
    std::vector<CardIndex> indexes(cards.size());
    std::iota(indexes.begin(), indexes.end(), CardIndex{0});

    std::stable_sort(
        indexes.begin(),
        indexes.end(),
        [&cards](CardIndex left, CardIndex right) {
            return cards[left].order < cards[right].order;
        }
    );

    return indexes;
}

std::map<std::string, const WorkingCard*> cardMap(
    const std::vector<WorkingCard>& cards,
    bool enforceExportLimit
)
{
    std::map<std::string, const WorkingCard*> result;
    std::size_t total = 0;

    for (const WorkingCard& card : cards) {
        if (
            card.id.empty()
            || card.quantity < 1
            || card.order < 0
            || result.contains(card.id)
        ) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalOrder,
                "Physical order requires unique WorkingCards with positive quantities and non-negative logical order."
            );
        }

        result.emplace(card.id, &card);
        total += card.quantity;
    }

    if (enforceExportLimit && total > MaxPhysicalCardsPerExport) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::ExportLimitExceeded,
            "A Project cannot contain more than 500 physical cards."
        );
    }

    return result;
}

PhysicalInstanceRef newReference(
    PhysicalInstanceId nextInstanceId,
    const std::string& workingCardId
)
{
    if (nextInstanceId < 1 || nextInstanceId > MaxPersistedPhysicalInstanceId) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::InvalidPhysicalOrder,
            "The next physical instance ID must be positive."
        );
    }

    return PhysicalInstanceRef{
        .id = nextInstanceId,
        .workingCardId = workingCardId,
    };
}

std::size_t totalCopies(const std::map<std::string, const WorkingCard*>& cards)
{
    std::size_t total = 0;
    for (const auto& [id, card] : cards) {
        static_cast<void>(id);
        total += card->quantity;
    }
    return total;
}

} // namespace

PhysicalOrderError::PhysicalOrderError(
    PhysicalOrderErrorCode code,
    std::string message
)
    : std::runtime_error(std::move(message)),
      code_(code)
{
}

PhysicalOrderErrorCode PhysicalOrderError::code() const noexcept
{
    return code_;
}

std::string physicalInstanceIdString(PhysicalInstanceId id)
{
    return "instance-" + std::to_string(id);
}

std::optional<PhysicalInstanceId> parsePhysicalInstanceIdString(
    const std::string& id
) noexcept
{
    constexpr std::string_view Prefix = "instance-";

    if (!id.starts_with(Prefix)) {
        return std::nullopt;
    }

    const std::string_view digits{id.data() + Prefix.size(), id.size() - Prefix.size()};
    if (digits.empty() || digits.size() > 16 || digits.front() == '0') {
        return std::nullopt;
    }

    PhysicalInstanceId value = 0;
    const auto result = std::from_chars(
        digits.data(),
        digits.data() + digits.size(),
        value
    );

    if (
        result.ec != std::errc{}
        || result.ptr != digits.data() + digits.size()
        || value < 1
        || value > MaxPersistedPhysicalInstanceId
    ) {
        return std::nullopt;
    }

    return value;
}

PhysicalOrder makeLegacyPhysicalOrder(const std::vector<WorkingCard>& cards)
{
    const auto byId = cardMap(cards, false);
    PhysicalOrder result;

    for (const CardIndex index : orderedCardIndexes(cards)) {
        const WorkingCard& card = cards[index];

        for (std::uint32_t copy = 0; copy < card.quantity; ++copy) {
            result.instances.push_back(
                newReference(result.nextInstanceId++, card.id)
            );
        }
    }

    if (result.instances.size() != totalCopies(byId)) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::InvalidPhysicalOrder,
            "Physical order could not be initialized from WorkingCards."
        );
    }

    return result;
}

PhysicalOrder validatePhysicalOrder(
    const std::vector<WorkingCard>& cards,
    const PhysicalOrder& order
)
{
    const auto byId = cardMap(cards, true);

    if (
        order.nextInstanceId < 1
        || order.instances.size() != totalCopies(byId)
        || order.instances.size() > MaxPhysicalCardsPerExport
    ) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::InvalidPhysicalOrder,
            "Physical instance references must match WorkingCard quantities exactly."
        );
    }

    std::set<PhysicalInstanceId> ids;
    std::map<std::string, std::size_t> counts;
    PhysicalInstanceId maxId = 0;

    for (const PhysicalInstanceRef& instance : order.instances) {
        if (instance.id < 1 || instance.id > MaxPersistedPhysicalInstanceId) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalInstanceId,
                "Physical instance ID is malformed."
            );
        }

        if (!ids.insert(instance.id).second) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalOrder,
                "Physical instance ID is duplicated."
            );
        }

        maxId = std::max(maxId, instance.id);

        if (!byId.contains(instance.workingCardId)) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalOrder,
                "Physical instance references an unknown WorkingCard."
            );
        }

        ++counts[instance.workingCardId];
    }

    for (const auto& [id, card] : byId) {
        if (counts[id] != card->quantity) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalOrder,
                "Physical instance references do not match WorkingCard quantity."
            );
        }
    }

    if (order.nextInstanceId <= maxId) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::InvalidPhysicalOrder,
            "nextInstanceId must be greater than every allocated physical instance ID."
        );
    }

    return order;
}

PhysicalOrder reconcilePhysicalOrder(
    const std::vector<WorkingCard>& cards,
    const std::optional<PhysicalOrder>& previous
)
{
    const auto byId = cardMap(cards, false);

    if (!previous) {
        return makeLegacyPhysicalOrder(cards);
    }

    std::vector<PhysicalInstanceRef> retained;
    std::map<std::string, std::size_t> counts;
    std::set<PhysicalInstanceId> ids;
    PhysicalInstanceId nextInstanceId = std::max<PhysicalInstanceId>(
        1,
        previous->nextInstanceId
    );

    for (const PhysicalInstanceRef& instance : previous->instances) {
        if (instance.id < 1) {
            continue;
        }

        if (instance.id >= MaxPersistedPhysicalInstanceId) {
            continue;
        }

        nextInstanceId = std::max(nextInstanceId, instance.id + 1);

        const auto cardIterator = byId.find(instance.workingCardId);
        if (cardIterator == byId.end()) {
            continue;
        }

        const WorkingCard& card = *cardIterator->second;
        const std::size_t count = counts[instance.workingCardId];

        if (count >= card.quantity || ids.contains(instance.id)) {
            continue;
        }

        retained.push_back(instance);
        counts[instance.workingCardId] = count + 1;
        ids.insert(instance.id);
    }

    std::vector<PhysicalInstanceRef> refs = retained;

    for (const CardIndex cardIndex : orderedCardIndexes(cards)) {
        const WorkingCard& card = cards[cardIndex];
        std::size_t current = counts[card.id];

        while (current < card.quantity) {
            const PhysicalInstanceRef reference =
                newReference(nextInstanceId++, card.id);

            std::optional<std::size_t> lastSame;
            std::optional<std::size_t> nextLogical;

            for (std::size_t index = 0; index < refs.size(); ++index) {
                const auto mapped = byId.find(refs[index].workingCardId);
                if (refs[index].workingCardId == card.id) {
                    lastSame = index;
                }

                if (
                    !nextLogical
                    && mapped != byId.end()
                    && mapped->second->order > card.order
                ) {
                    nextLogical = index;
                }
            }

            const std::size_t insertionIndex = lastSame
                ? *lastSame + 1
                : nextLogical.value_or(refs.size());

            refs.insert(
                refs.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
                reference
            );

            ++current;
            counts[card.id] = current;
        }
    }

    PhysicalOrder result{
        .nextInstanceId = nextInstanceId,
        .instances = std::move(refs),
    };

    if (totalCopies(byId) > MaxPhysicalCardsPerExport) {
        return result;
    }

    return validatePhysicalOrder(cards, result);
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

AddPhysicalInstanceResult addPhysicalInstance(
    const PhysicalOrder& order,
    const std::string& workingCardId,
    std::optional<std::size_t> afterIndex
)
{
    if (order.instances.size() >= MaxPhysicalCardsPerExport) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::ExportLimitExceeded,
            "A Project cannot contain more than 500 physical cards."
        );
    }

    const PhysicalInstanceRef instance =
        newReference(order.nextInstanceId, workingCardId);

    std::size_t insertionAfter = 0;

    if (afterIndex) {
        if (*afterIndex >= order.instances.size()) {
            throw PhysicalOrderError(
                PhysicalOrderErrorCode::InvalidPhysicalOrder,
                "Physical instance insertion position is invalid."
            );
        }
        insertionAfter = *afterIndex;
    } else {
        bool found = false;
        for (std::size_t index = 0; index < order.instances.size(); ++index) {
            if (order.instances[index].workingCardId == workingCardId) {
                insertionAfter = index;
                found = true;
            }
        }

        if (!found) {
            insertionAfter = order.instances.empty()
                ? 0
                : order.instances.size() - 1;
        }
    }

    PhysicalOrder result = order;
    const std::size_t insertionIndex = order.instances.empty()
        ? 0
        : insertionAfter + 1;

    result.instances.insert(
        result.instances.begin()
            + static_cast<std::ptrdiff_t>(insertionIndex),
        instance
    );
    ++result.nextInstanceId;

    return AddPhysicalInstanceResult{
        .order = std::move(result),
        .instance = instance,
    };
}

RemovePhysicalInstanceResult removePhysicalInstance(
    const PhysicalOrder& order,
    PhysicalInstanceId instanceId
)
{
    const auto index = physicalInstanceIndex(order, instanceId);
    if (!index) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::PhysicalInstanceNotFound,
            "Physical instance was not found."
        );
    }

    PhysicalOrder result = order;
    const PhysicalInstanceRef removed = result.instances[*index];
    result.instances.erase(
        result.instances.begin() + static_cast<std::ptrdiff_t>(*index)
    );

    return RemovePhysicalInstanceResult{
        .order = std::move(result),
        .removed = removed,
    };
}

PhysicalOrder replacePhysicalInstanceCard(
    const PhysicalOrder& order,
    PhysicalInstanceId instanceId,
    std::string workingCardId
)
{
    PhysicalOrder result = order;
    const auto index = physicalInstanceIndex(result, instanceId);

    if (!index) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::PhysicalInstanceNotFound,
            "Physical instance was not found."
        );
    }

    result.instances[*index].workingCardId = std::move(workingCardId);
    return result;
}

PhysicalOrder movePhysicalInstance(
    const PhysicalOrder& order,
    PhysicalInstanceId sourceId,
    std::optional<PhysicalInstanceId> targetId,
    InsertPosition position
)
{
    const auto sourceIndex = physicalInstanceIndex(order, sourceId);
    if (!sourceIndex) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::PhysicalInstanceNotFound,
            "Physical instance was not found."
        );
    }

    PhysicalOrder result = order;
    const PhysicalInstanceRef moved = result.instances[*sourceIndex];
    result.instances.erase(
        result.instances.begin() + static_cast<std::ptrdiff_t>(*sourceIndex)
    );

    if (!targetId) {
        result.instances.push_back(moved);
        return result;
    }

    const auto targetIndex = physicalInstanceIndex(result, *targetId);
    if (!targetIndex) {
        throw PhysicalOrderError(
            PhysicalOrderErrorCode::PhysicalInstanceNotFound,
            "Target physical instance was not found."
        );
    }

    std::size_t insertionIndex = *targetIndex;
    if (position == InsertPosition::After) {
        ++insertionIndex;
    }

    result.instances.insert(
        result.instances.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        moved
    );

    return result;
}

} // namespace tcgprint::cards
