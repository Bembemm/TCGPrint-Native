#pragma once

#include "domain/cards/PhysicalOrder.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcgprint::cards {

enum class WorkingSetErrorCode
{
    CardNotFound,
    InvalidQuantity,
    ExportLimitExceeded,
    InvalidTargetIndex,
    DuplicateId,
    CardIdMismatch,
};

class WorkingSetError final : public std::runtime_error
{
public:
    WorkingSetError(WorkingSetErrorCode code, std::string message);

    [[nodiscard]] WorkingSetErrorCode code() const noexcept;

private:
    WorkingSetErrorCode code_;
};

struct WorkingSetState final
{
    std::vector<WorkingCard> cards;
    std::optional<std::string> selectedCardId;
    PhysicalOrder physicalOrder;

    bool operator==(const WorkingSetState&) const = default;
};

[[nodiscard]] std::vector<WorkingCard> normalizeWorkingCardOrder(
    const std::vector<WorkingCard>& cards
);

[[nodiscard]] WorkingSetState createWorkingSetState(
    const std::vector<WorkingCard>& cards,
    std::optional<std::string> selectedCardId = std::nullopt,
    std::optional<PhysicalOrder> physicalOrder = std::nullopt
);

[[nodiscard]] WorkingSetState selectWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId
);

[[nodiscard]] WorkingSetState replaceWorkingCards(
    const WorkingSetState& state,
    const std::vector<WorkingCard>& cards,
    std::optional<PhysicalOrder> physicalOrder = std::nullopt
);

[[nodiscard]] WorkingSetState replaceWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    const WorkingCard& nextCard,
    std::optional<PhysicalOrder> physicalOrder = std::nullopt
);

[[nodiscard]] WorkingSetState setWorkingCardQuantity(
    const WorkingSetState& state,
    const std::string& cardId,
    std::uint32_t quantity
);

[[nodiscard]] WorkingSetState removeWorkingCardPhysicalInstance(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId
);

[[nodiscard]] WorkingSetState reorderWorkingCardPhysicalInstance(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId,
    std::optional<PhysicalInstanceId> targetInstanceId,
    InsertPosition position = InsertPosition::After
);

[[nodiscard]] WorkingSetState moveWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    std::size_t targetIndex
);

[[nodiscard]] WorkingSetState duplicateWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId,
    const std::string& newCardId
);

[[nodiscard]] WorkingSetState duplicatePhysicalInstanceAsEntry(
    const WorkingSetState& state,
    PhysicalInstanceId instanceId,
    const std::string& newCardId
);

[[nodiscard]] WorkingSetState deleteWorkingCard(
    const WorkingSetState& state,
    const std::string& cardId
);

struct WorkingSetHistory final
{
    WorkingSetState present;
    std::vector<WorkingSetState> past;
    std::vector<WorkingSetState> future;

    bool operator==(const WorkingSetHistory&) const = default;
};

[[nodiscard]] WorkingSetHistory createWorkingSetHistory(
    const WorkingSetState& present
);

[[nodiscard]] WorkingSetHistory updateWorkingSetHistoryPresent(
    const WorkingSetHistory& history,
    const WorkingSetState& present
);

[[nodiscard]] WorkingSetHistory commitWorkingSetHistory(
    const WorkingSetHistory& history,
    const WorkingSetState& present
);

[[nodiscard]] WorkingSetHistory undoWorkingSetHistory(
    const WorkingSetHistory& history
);

[[nodiscard]] WorkingSetHistory redoWorkingSetHistory(
    const WorkingSetHistory& history
);

[[nodiscard]] WorkingSetHistory resetWorkingSetHistory(
    const WorkingSetHistory& history,
    const WorkingSetState& present
);

} // namespace tcgprint::cards
