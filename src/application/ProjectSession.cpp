#include "application/ProjectSession.h"

#include "persistence/projects/ProjectSnapshot.h"

#include <utility>

namespace tcgprint::application {
namespace {

QByteArray snapshotKey(
    const projects::ProjectSnapshotCompat& snapshot
)
{
    return projects::serializeProjectSnapshot(snapshot);
}

} // namespace

ProjectSessionError::ProjectSessionError(
    ProjectSessionErrorCode code,
    std::string message
)
    : std::runtime_error(std::move(message)),
      code_(code)
{
}

ProjectSessionErrorCode ProjectSessionError::code() const noexcept
{
    return code_;
}

ProjectSession::ProjectSession(
    projects::ProjectRepository& repository
)
    : repository_(&repository)
{
}

const ProjectSessionState& ProjectSession::state() const noexcept
{
    return state_;
}

projects::ProjectRecord& ProjectSession::activeProject()
{
    if (!state_.activeProject) {
        throw ProjectSessionError(
            ProjectSessionErrorCode::NoActiveProject,
            "No Project is active."
        );
    }

    return *state_.activeProject;
}

const projects::ProjectSnapshotCompat&
ProjectSession::currentSnapshot() const
{
    if (!state_.currentSnapshot) {
        throw ProjectSessionError(
            ProjectSessionErrorCode::NoActiveProject,
            "No Project snapshot is active."
        );
    }

    return *state_.currentSnapshot;
}

void ProjectSession::activate(projects::ProjectRecord project)
{
    const QByteArray key = snapshotKey(project.snapshot);

    state_.activeProject = std::move(project);
    state_.currentSnapshot = state_.activeProject->snapshot;
    state_.recovery = repository_->readRecovery(
        state_.activeProject->metadata.id
    );
    state_.currentSnapshotKey = key;
    state_.savedSnapshotKey = key;
    state_.status = ProjectSaveStatus::Saved;
    state_.error.reset();
}

const ProjectSessionState& ProjectSession::open(
    const std::string& projectId
)
{
    activate(repository_->open(projectId));
    return state_;
}

const ProjectSessionState& ProjectSession::create(
    const projects::ProjectSnapshotCompat& snapshot,
    std::string name
)
{
    activate(
        repository_->create(
            snapshot,
            std::move(name)
        )
    );
    return state_;
}

void ProjectSession::replaceSnapshot(
    const projects::ProjectSnapshotCompat& snapshot
)
{
    static_cast<void>(activeProject());

    state_.currentSnapshot = snapshot;
    state_.currentSnapshotKey = snapshotKey(snapshot);
    state_.status = isDirty()
        ? ProjectSaveStatus::Dirty
        : ProjectSaveStatus::Saved;

    if (state_.status == ProjectSaveStatus::Saved) {
        state_.error.reset();
    } else if (
        state_.status == ProjectSaveStatus::Dirty
        && state_.error
    ) {
        state_.error.reset();
    }
}

bool ProjectSession::isDirty() const noexcept
{
    return state_.activeProject.has_value()
        && state_.currentSnapshot.has_value()
        && state_.currentSnapshotKey != state_.savedSnapshotKey;
}

projects::ProjectRecord ProjectSession::save()
{
    projects::ProjectRecord& active = activeProject();

    if (!isDirty()) {
        state_.status = ProjectSaveStatus::Saved;
        return active;
    }

    state_.status = ProjectSaveStatus::Saving;
    state_.error.reset();

    try {
        static_cast<void>(
            repository_->stageRecovery(
                active.metadata.id,
                active.metadata.revision,
                currentSnapshot()
            )
        );

        projects::ProjectRecord saved =
            repository_->promoteRecovery(
                active.metadata.id
            );

        activate(saved);
        return saved;
    } catch (const projects::ProjectRepositoryError& error) {
        state_.error = error.what();

        if (
            error.code()
            == projects::ProjectRepositoryErrorCode::
                ProjectRevisionConflict
        ) {
            state_.status = ProjectSaveStatus::Conflict;
        } else {
            state_.status = ProjectSaveStatus::Error;
        }

        throw;
    } catch (const std::exception& error) {
        state_.error = error.what();
        state_.status = ProjectSaveStatus::Error;
        throw;
    }
}

projects::ProjectRecord ProjectSession::autosave()
{
    return save();
}

projects::ProjectRecord ProjectSession::resolveRecovery(
    ProjectRecoveryChoice choice
)
{
    projects::ProjectRecord& active = activeProject();

    if (!state_.recovery) {
        throw ProjectSessionError(
            ProjectSessionErrorCode::NoRecoveryCandidate,
            "The active Project has no recovery candidate."
        );
    }

    const bool current =
        state_.recovery->baseRevision
        == active.metadata.revision;

    projects::ProjectRecord resolved;

    if (choice == ProjectRecoveryChoice::Discard) {
        repository_->discardRecovery(active.metadata.id);
        resolved = repository_->open(active.metadata.id);
    } else if (
        choice == ProjectRecoveryChoice::Restore
        && current
    ) {
        resolved =
            repository_->promoteRecovery(active.metadata.id);
    } else if (
        choice == ProjectRecoveryChoice::Copy
        && !current
    ) {
        resolved =
            repository_->copyRecovery(active.metadata.id);
    } else {
        throw ProjectSessionError(
            ProjectSessionErrorCode::InvalidRecoveryChoice,
            current
                ? "A current recovery must be restored or discarded."
                : "A stale recovery must be copied or discarded."
        );
    }

    activate(resolved);
    return resolved;
}

} // namespace tcgprint::application
