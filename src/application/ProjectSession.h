#pragma once

#include "persistence/projects/ProjectRepository.h"

#include <QByteArray>

#include <optional>
#include <stdexcept>
#include <string>

namespace tcgprint::application {

enum class ProjectSaveStatus
{
    Dirty,
    Saving,
    Saved,
    Error,
    Conflict,
};

enum class ProjectRecoveryChoice
{
    Restore,
    Copy,
    Discard,
};

enum class ProjectSessionErrorCode
{
    NoActiveProject,
    NoRecoveryCandidate,
    InvalidRecoveryChoice,
};

class ProjectSessionError final : public std::runtime_error
{
public:
    ProjectSessionError(
        ProjectSessionErrorCode code,
        std::string message
    );

    [[nodiscard]] ProjectSessionErrorCode code() const noexcept;

private:
    ProjectSessionErrorCode code_;
};

struct ProjectSessionState final
{
    std::optional<projects::ProjectRecord> activeProject;
    std::optional<projects::ProjectSnapshotCompat> currentSnapshot;
    std::optional<projects::ProjectRecoveryRecord> recovery;
    QByteArray currentSnapshotKey;
    QByteArray savedSnapshotKey;
    ProjectSaveStatus status{ProjectSaveStatus::Saved};
    std::optional<std::string> error;
};

class ProjectSession final
{
public:
    explicit ProjectSession(projects::ProjectRepository& repository);

    [[nodiscard]] const ProjectSessionState& state() const noexcept;

    [[nodiscard]] const ProjectSessionState& open(
        const std::string& projectId
    );

    [[nodiscard]] const ProjectSessionState& create(
        const projects::ProjectSnapshotCompat& snapshot,
        std::string name = "Novo projeto"
    );

    void replaceSnapshot(
        const projects::ProjectSnapshotCompat& snapshot
    );

    [[nodiscard]] bool isDirty() const noexcept;

    [[nodiscard]] projects::ProjectRecord save();

    [[nodiscard]] projects::ProjectRecord autosave();

    [[nodiscard]] projects::ProjectRecord resolveRecovery(
        ProjectRecoveryChoice choice
    );

private:
    projects::ProjectRepository* repository_;
    ProjectSessionState state_;

    [[nodiscard]] projects::ProjectRecord& activeProject();
    [[nodiscard]] const projects::ProjectSnapshotCompat& currentSnapshot() const;
    void activate(projects::ProjectRecord project);
};

} // namespace tcgprint::application
