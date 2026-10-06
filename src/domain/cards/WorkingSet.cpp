#include "domain/cards/WorkingSet.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace tcgprint::cards {
namespace {

WorkingSetError cardNotFound(const std::string& cardId)
{
    return WorkingSetError(
        WorkingSetErrorCode::CardNotFound,
        "WorkingCard " + cardId + " was not found."
    );
}

std::size_t physicalTotal(const std::vector<WorkingCard>& cards)
{
    return std::accumulate(
        cards.begin(),
        cards.end(),
        std::size_t{0},
        [](std::size_t total, const WorkingCard& card) {
            return total + card.quantity;
        }
    );
}

WorkingSetState stateWithCards(
    const WorkingSetState& state,
    const std::vector<WorkingCard>& cards,
    const std::optional<std::string>& selectedCardId,
    const std::optional<PhysicalOrder>& physicalOrder
)
{
    std::vector<WorkingCard> normalized =
        normalizeWorkingCardOrder(cards);

    std::optional<std::string> selection;
    if (selectedCardId) {
        const bool exists = std::any_of(
            normalized.begin(),
            normalized.end(),
            [&selectedCardId](const WorkingCard& card) {
                return card.id == *selectedCardId;
            }
        );
        if (exists) {
            selection = selectedCardId;
        }
    }

    if (!selection && !normalized.empty()) {
        selection = normalized.front().id;
    }

    return WorkingSetState{
        .cards = normalized,
        .selectedCardId = selection,
        .physicalOrder = reconcilePhysicalOrder(
            normalized,
            physicalOrder
                ? physicalOrder
                : std::optional<PhysicalOrder>{state.physicalOrder}
        ),
    };
}

std::size_t cardIndex(
    const std::vector<WorkingCard>& cards,
    const std::string& cardId
)
{
    const auto iterator = std::find_if(
        cards.begin(),
        cards.end(),
        [&cardId](const WorkingCard& card) {
            return card.id == cardId;
        }
    );

    if (iterator == cards.end()) {
        throw cardNotFound(cardId);
    }

    return static_cast<std::size_t>(
        std::distance(cards.begin(), iterator)
    );
}

void assertNewCardId(
    const std::vector<WorkingCard>& cards,
    const std::string& newCardId
)
{
    if (
        newCardId.empty()
        || std::any_of(
            cards.begin(),
            cards.end(),
            [&newCardId](const WorkingCard& card) {
                return card.id == newCardId;
            }
        )
    ) {
        throw WorkingSetError(
            WorkingSetErrorCode::DuplicateId,
            "WorkingCard ID is empty or already exists."
        );
    }
}

} // namespace

WorkingSetError::WorkingSetError(
    WorkingSetErrorCode code,
    std::string message
)
    : std::runtime_error(std::move(message)),
      code_(code)
{
}

WorkingSetErrorCode WorkingSetError::code() const noexcept
{
    return code_;
}

std::vector<WorkingCard> normalizeWorkingCardOrder(
    const std::vector<WorkingCard>& cards
)
{
    struct IndexedCard final
    {
        WorkingCard card;
        std::size_t originalIndex;
    };

    std::vector<IndexedCard> ordered;
    ordered.reserve(cards.size());

    for (std::size_t index = 0; index < cards.size(); ++index) {
        ordered.push_back(IndexedCard{
            .card = cards[index],
            .originalIndex = index,
        });
    }

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const IndexedCard& left, const IndexedCard& right) {
            if (left.card.order != right.card.order) {
                return left.card.order < right.card.order;
            }
            return left.originalIndex < right.originalIndex;
        }
    );

    std::vector<WorkingCard> result;
    result.reserve(ordered.size());

    for (std::size_t order = 0; order < ordered.size(); ++order) {
        WorkingCard card = std::move(ordered[order].card);
        card.order = static_cast<int>(order);
        result.push_back(std::move(card));
    }

    return result;
}

WorkingSetState createWorkingSetState(
    const std::vector<WorkingCard>& cards,
    std::optional<std::string> selectedCardId,
    std::optional<PhysicalOrder> physicalOrder
)
{
    const std::vector<WorkingCard> normalized =
        normalizeWorkingCardOrder(cards);

    if (!selectedCardId && !normalized.empty()) {
        selectedCardId = normalized.front().id;
    }

    WorkingSetState seed{
        .cards = normalized,
        .selectedCardId = selectedCardId,
        .physicalOrder = physicalOrder
            ? *physicalOrder
            : reconcilePhysicalOrder(normalized),
    };

    return stateWithCards(
        seed,
        normalized,
        selectedCardId,
        physicalOrder
    );
}

WorkingSetState selectWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId
)
{
    static_cast<void>(cardIndex(state.cards, cardId));

    WorkingSetState result = state;
    result.cards = normalizeWorkingCardOrder(state.cards);
    result.selectedCardId = cardId;
    return result;
}

WorkingSetState replaceWorkingCards(
    const WorkingSetState& state,
    const std::vector<WorkingCard>& cards,
    std::optional<PhysicalOrder> physicalOrder
)
{
    return stateWithCards(
        state,
        cards,
        state.selectedCardId,
        physicalOrder
    );
}

WorkingSetState replaceWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    const WorkingCard& nextCard,
    std::optional<PhysicalOrder> physicalOrder
)
{
    static_cast<void>(cardIndex(state.cards, cardId));

    if (nextCard.id != cardId) {
        throw WorkingSetError(
            WorkingSetErrorCode::CardIdMismatch,
            "Replacing a WorkingCard must preserve its ID."
        );
    }

    std::vector<WorkingCard> cards = state.cards;
    cards[cardIndex(cards, cardId)] = nextCard;

    return stateWithCards(
        state,
        cards,
        state.selectedCardId,
        physicalOrder
    );
}

WorkingSetState setWorkingCardQuantity(
    const WorkingSetState& state,
    const std::string& cardId,
    std::uint32_t quantity
)
{
    const std::size_t index = cardIndex(state.cards, cardId);
    const WorkingCard& card = state.cards[index];

    if (quantity < 1) {
        throw WorkingSetError(
            WorkingSetErrorCode::InvalidQuantity,
            "Quantity must be an integer of at least 1."
        );
    }

    if (quantity == card.quantity) {
        return state;
    }

    const std::size_t total = physicalTotal(state.cards);
    const std::size_t nextTotal =
        total - card.quantity + quantity;

    if (
        nextTotal > MaxPhysicalCardsPerExport
        && quantity > card.quantity
    ) {
        throw WorkingSetError(
            WorkingSetErrorCode::ExportLimitExceeded,
            "Quantity would exceed the 500-card export limit."
        );
    }

    std::vector<WorkingCard> cards = state.cards;
    cards[index].quantity = quantity;

    return stateWithCards(
        state,
        cards,
        state.selectedCardId,
        std::nullopt
    );
}

WorkingSetState removeWorkingCardPhysicalInstance(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId
)
{
    const auto index =
        physicalInstanceIndex(state.physicalOrder, instanceId);

    if (!index) {
        throw WorkingSetError(
            WorkingSetErrorCode::CardNotFound,
            "Physical instance was not found."
        );
    }

    const PhysicalInstanceRef& reference =
        state.physicalOrder.instances[*index];
    const std::size_t workingCardIndex =
        cardIndex(state.cards, reference.workingCardId);

    if (state.cards[workingCardIndex].quantity == 1) {
        return deleteWorkingCard(
            state,
            reference.workingCardId
        );
    }

    const RemovePhysicalInstanceResult removed =
        removePhysicalInstance(state.physicalOrder, instanceId);

    std::vector<WorkingCard> cards = state.cards;
    --cards[workingCardIndex].quantity;

    return stateWithCards(
        state,
        cards,
        state.selectedCardId,
        removed.order
    );
}

WorkingSetState reorderWorkingCardPhysicalInstance(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId,
    std::optional<PhysicalInstanceId> targetInstanceId,
    InsertPosition position
)
{
    WorkingSetState result = state;
    result.physicalOrder = movePhysicalInstance(
        state.physicalOrder,
        instanceId,
        targetInstanceId,
        position
    );
    return result;
}

WorkingSetState moveWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    std::size_t targetIndex
)
{
    std::vector<WorkingCard> cards =
        normalizeWorkingCardOrder(state.cards);
    const std::size_t currentIndex =
        cardIndex(cards, cardId);

    if (targetIndex >= cards.size()) {
        throw WorkingSetError(
            WorkingSetErrorCode::InvalidTargetIndex,
            "Target index is outside the Working Set."
        );
    }

    if (currentIndex == targetIndex) {
        WorkingSetState result = state;
        result.cards = std::move(cards);
        return result;
    }

    WorkingCard moved = cards[currentIndex];
    cards.erase(
        cards.begin() + static_cast<std::ptrdiff_t>(currentIndex)
    );
    cards.insert(
        cards.begin() + static_cast<std::ptrdiff_t>(targetIndex),
        moved
    );

    for (std::size_t index = 0; index < cards.size(); ++index) {
        cards[index].order = static_cast<int>(index);
    }

    std::vector<PhysicalInstanceRef> movingRefs;
    std::vector<PhysicalInstanceRef> remainingRefs;

    for (const PhysicalInstanceRef& reference :
         state.physicalOrder.instances) {
        if (reference.workingCardId == cardId) {
            movingRefs.push_back(reference);
        } else {
            remainingRefs.push_back(reference);
        }
    }

    std::size_t insertionIndex = remainingRefs.size();

    if (targetIndex + 1 < cards.size()) {
        const std::string& nextCardId =
            cards[targetIndex + 1].id;

        const auto iterator = std::find_if(
            remainingRefs.begin(),
            remainingRefs.end(),
            [&nextCardId](const PhysicalInstanceRef& reference) {
                return reference.workingCardId == nextCardId;
            }
        );

        if (iterator != remainingRefs.end()) {
            insertionIndex = static_cast<std::size_t>(
                std::distance(
                    remainingRefs.begin(),
                    iterator
                )
            );
        }
    }

    std::vector<PhysicalInstanceRef> instances;
    instances.reserve(state.physicalOrder.instances.size());
    instances.insert(
        instances.end(),
        remainingRefs.begin(),
        remainingRefs.begin()
            + static_cast<std::ptrdiff_t>(insertionIndex)
    );
    instances.insert(
        instances.end(),
        movingRefs.begin(),
        movingRefs.end()
    );
    instances.insert(
        instances.end(),
        remainingRefs.begin()
            + static_cast<std::ptrdiff_t>(insertionIndex),
        remainingRefs.end()
    );

    PhysicalOrder order = state.physicalOrder;
    order.instances = std::move(instances);

    return stateWithCards(
        state,
        cards,
        state.selectedCardId,
        order
    );
}

WorkingSetState duplicateWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    const std::string& newCardId
)
{
    const std::vector<WorkingCard> cards =
        normalizeWorkingCardOrder(state.cards);
    const std::size_t originalIndex =
        cardIndex(cards, cardId);

    assertNewCardId(cards, newCardId);

    const WorkingCard& original = cards[originalIndex];
    if (
        physicalTotal(cards) + original.quantity
        > MaxPhysicalCardsPerExport
    ) {
        throw WorkingSetError(
            WorkingSetErrorCode::ExportLimitExceeded,
            "Duplicating this entry would exceed the export limit."
        );
    }

    WorkingCard clone = original;
    clone.id = newCardId;

    std::vector<WorkingCard> nextCards = cards;
    nextCards.insert(
        nextCards.begin()
            + static_cast<std::ptrdiff_t>(originalIndex + 1),
        clone
    );

    PhysicalOrder order = state.physicalOrder;

    std::size_t lastOriginalIndex = 0;
    bool foundOriginal = false;
    for (std::size_t index = 0;
         index < order.instances.size();
         ++index) {
        if (order.instances[index].workingCardId == cardId) {
            lastOriginalIndex = index;
            foundOriginal = true;
        }
    }

    if (!foundOriginal) {
        throw WorkingSetError(
            WorkingSetErrorCode::CardNotFound,
            "WorkingCard has no physical instances."
        );
    }

    for (std::uint32_t copy = 0;
         copy < original.quantity;
         ++copy) {
        const AddPhysicalInstanceResult added =
            addPhysicalInstance(
                order,
                newCardId,
                lastOriginalIndex
            );

        order = added.order;
        const auto addedIndex =
            physicalInstanceIndex(order, added.instance.id);
        if (!addedIndex) {
            throw WorkingSetError(
                WorkingSetErrorCode::CardNotFound,
                "New physical instance could not be located."
            );
        }
        lastOriginalIndex = *addedIndex;
    }

    return stateWithCards(
        state,
        nextCards,
        newCardId,
        order
    );
}

WorkingSetState duplicatePhysicalInstanceAsEntry(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId,
    const std::string& newCardId
)
{
    const auto physicalIndex =
        physicalInstanceIndex(state.physicalOrder, instanceId);

    if (!physicalIndex) {
        throw WorkingSetError(
            WorkingSetErrorCode::CardNotFound,
            "Physical instance was not found."
        );
    }

    assertNewCardId(state.cards, newCardId);

    if (
        state.physicalOrder.instances.size()
        >= MaxPhysicalCardsPerExport
    ) {
        throw WorkingSetError(
            WorkingSetErrorCode::ExportLimitExceeded,
            "Duplicating this physical instance would exceed the export limit."
        );
    }

    const PhysicalInstanceRef& reference =
        state.physicalOrder.instances[*physicalIndex];
    const std::size_t sourceIndex =
        cardIndex(state.cards, reference.workingCardId);

    WorkingCard clone = state.cards[sourceIndex];
    clone.id = newCardId;
    clone.quantity = 1;

    std::vector<WorkingCard> cards = state.cards;
    cards.insert(
        cards.begin()
            + static_cast<std::ptrdiff_t>(sourceIndex + 1),
        clone
    );

    const AddPhysicalInstanceResult added =
        addPhysicalInstance(
            state.physicalOrder,
            newCardId,
            *physicalIndex
        );

    return stateWithCards(
        state,
        cards,
        newCardId,
        added.order
    );
}

WorkingSetState deleteWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId
)
{
    std::vector<WorkingCard> cards =
        normalizeWorkingCardOrder(state.cards);
    const std::size_t deleteIndex =
        cardIndex(cards, cardId);

    cards.erase(
        cards.begin() + static_cast<std::ptrdiff_t>(deleteIndex)
    );

    std::optional<std::string> selection =
        state.selectedCardId;

    if (selection && *selection == cardId) {
        if (deleteIndex < cards.size()) {
            selection = cards[deleteIndex].id;
        } else if (!cards.empty()) {
            selection = cards.back().id;
        } else {
            selection = std::nullopt;
        }
    }

    return stateWithCards(
        state,
        cards,
        selection,
        std::nullopt
    );
}

WorkingSetHistory createWorkingSetHistory(
    const WorkingSetState& present
)
{
    return WorkingSetHistory{
        .present = present,
        .past = {},
        .future = {},
    };
}

WorkingSetHistory updateWorkingSetHistoryPresent(
    const WorkingSetHistory& history,
    const WorkingSetState& present
)
{
    if (history.present == present) {
        return history;
    }

    WorkingSetHistory result = history;
    result.present = present;
    return result;
}

WorkingSetHistory commitWorkingSetHistory(
    const WorkingSetHistory& history,
    const WorkingSetState& present
)
{
    if (history.present == present) {
        return history;
    }

    WorkingSetHistory result = history;
    result.past.push_back(history.present);
    result.present = present;
    result.future.clear();
    return result;
}

WorkingSetHistory undoWorkingSetHistory(
    const WorkingSetHistory& history
)
{
    if (history.past.empty()) {
        return history;
    }

    WorkingSetHistory result = history;
    const WorkingSetState previous = result.past.back();
    result.past.pop_back();
    result.future.push_back(history.present);
    result.present = previous;
    return result;
}

WorkingSetHistory redoWorkingSetHistory(
    const WorkingSetHistory& history
)
{
    if (history.future.empty()) {
        return history;
    }

    WorkingSetHistory result = history;
    const WorkingSetState next = result.future.back();
    result.future.pop_back();
    result.past.push_back(history.present);
    result.present = next;
    return result;
}

WorkingSetHistory resetWorkingSetHistory(
    const WorkingSetHistory& history,
    const WorkingSetState& present
)
{
    static_cast<void>(history);
    return createWorkingSetHistory(present);
}

} // namespace tcgprint::cards
