#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tcgprint::cards {

struct CardIdentity final
{
    std::string provider;
    std::string id;

    bool operator==(const CardIdentity&) const = default;
};

enum class BackMode
{
    Auto,
    ProjectDefault,
    Manual,
    None,
};

struct WorkingCard final
{
    std::string id;
    std::string displayName;
    std::uint32_t quantity{1};
    int order{0};
    std::optional<CardIdentity> identity;
    BackMode backMode{BackMode::Auto};

    bool operator==(const WorkingCard&) const = default;
};

[[nodiscard]] inline bool isValidWorkingCard(const WorkingCard& card) noexcept
{
    return !card.id.empty() && card.quantity > 0;
}

} // namespace tcgprint::cards
