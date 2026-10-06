#pragma once

#include "domain/cards/PhysicalOrder.h"
#include "domain/projects/ProjectSettings.h"

#include <QByteArray>
#include <QJsonObject>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace tcgprint::projects {

inline constexpr int CurrentProjectSchemaVersion = 6;
inline constexpr std::size_t MaxProjectSnapshotBytes = 16 * 1024 * 1024;

enum class ProjectSnapshotErrorCode
{
    InvalidProjectSnapshot,
    InvalidProjectSchemaVersion,
    FutureProjectSchemaVersion,
    UnsupportedProjectSchemaVersion,
    ProjectSnapshotTooLarge,
};

class ProjectSnapshotError final : public std::runtime_error
{
public:
    ProjectSnapshotError(ProjectSnapshotErrorCode code, std::string message);

    [[nodiscard]] ProjectSnapshotErrorCode code() const noexcept;

private:
    ProjectSnapshotErrorCode code_;
};

struct PersistedWorkingCardCompat final
{
    std::string id;
    std::uint32_t quantity{1};
    int order{0};
    cards::BackMode backMode{cards::BackMode::ProjectDefault};
    cards::BackModeSelectionPolicy backModeSelectionPolicy{
        cards::BackModeSelectionPolicy::Automatic
    };
    QJsonObject raw;

    bool operator==(const PersistedWorkingCardCompat&) const = default;
};

struct ProjectSnapshotCompat final
{
    int sourceSchemaVersion{CurrentProjectSchemaVersion};
    int projectSchemaVersion{CurrentProjectSchemaVersion};
    std::vector<PersistedWorkingCardCompat> cards;
    std::vector<cards::WorkingCard> workingCards;
    QJsonObject settings;
    ProjectPrintSettings printSettings;
    cards::PhysicalOrder physicalOrder;
};

[[nodiscard]] ProjectSnapshotCompat deserializeProjectSnapshot(
    const QByteArray& json
);

[[nodiscard]] QByteArray serializeProjectSnapshot(
    const ProjectSnapshotCompat& snapshot
);

} // namespace tcgprint::projects
