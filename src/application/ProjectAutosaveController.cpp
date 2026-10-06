#include "application/ProjectAutosaveController.h"

#include <QString>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tcgprint::application {
namespace {

ProjectAutosaveTiming validatedTiming(
    ProjectAutosaveTiming timing
)
{
    if (timing.debounceMs < 0 || timing.maxWaitMs < 1) {
        throw std::invalid_argument(
            "Autosave timing must use non-negative debounce and positive max wait."
        );
    }

    if (
        std::any_of(
            timing.retryDelaysMs.begin(),
            timing.retryDelaysMs.end(),
            [](int delay) {
                return delay < 0;
            }
        )
    ) {
        throw std::invalid_argument(
            "Autosave retry delays must be non-negative."
        );
    }

    return timing;
}

} // namespace

ProjectAutosaveController::ProjectAutosaveController(
    ProjectSession& session,
    QObject* parent
)
    : ProjectAutosaveController(
          session,
          ProjectAutosaveTiming{},
          SaveInvoker{},
          parent
      )
{
}

ProjectAutosaveController::ProjectAutosaveController(
    ProjectSession& session,
    ProjectAutosaveTiming timing,
    SaveInvoker saveInvoker,
    QObject* parent
)
    : QObject(parent),
      session_(&session),
      timing_(validatedTiming(std::move(timing))),
      saveInvoker_(
          saveInvoker
              ? std::move(saveInvoker)
              : [&session]() {
                    return session.autosave();
                }
      )
{
    debounceTimer_.setSingleShot(true);
    maxWaitTimer_.setSingleShot(true);
    retryTimer_.setSingleShot(true);

    connect(
        &debounceTimer_,
        &QTimer::timeout,
        this,
        [this]() {
            static_cast<void>(attemptSave());
        }
    );

    connect(
        &maxWaitTimer_,
        &QTimer::timeout,
        this,
        [this]() {
            static_cast<void>(attemptSave());
        }
    );

    connect(
        &retryTimer_,
        &QTimer::timeout,
        this,
        [this]() {
            static_cast<void>(attemptSave());
        }
    );
}

void ProjectAutosaveController::observe(
    const projects::ProjectSnapshotCompat& snapshot
)
{
    session_->replaceSnapshot(snapshot);

    if (!session_->isDirty()) {
        pending_ = false;
        retryAttempt_ = 0;
        stopDebounceTimers();
        retryTimer_.stop();
        return;
    }

    pending_ = true;

    if (conflictBlocked_) {
        return;
    }

    schedule();
}

ProjectAutosaveFlushResult ProjectAutosaveController::flushNow()
{
    if (conflictBlocked_) {
        return ProjectAutosaveFlushResult::Conflict;
    }

    if (!pending_ || !session_->isDirty()) {
        pending_ = false;
        return ProjectAutosaveFlushResult::Idle;
    }

    stopDebounceTimers();
    retryTimer_.stop();
    return attemptSave();
}

void ProjectAutosaveController::reset()
{
    stopDebounceTimers();
    retryTimer_.stop();
    pending_ = false;
    conflictBlocked_ = false;
    retryAttempt_ = 0;
}

bool ProjectAutosaveController::hasPendingSave() const noexcept
{
    return pending_;
}

bool ProjectAutosaveController::isConflictBlocked() const noexcept
{
    return conflictBlocked_;
}

void ProjectAutosaveController::schedule()
{
    debounceTimer_.start(timing_.debounceMs);

    if (!maxWaitTimer_.isActive()) {
        maxWaitTimer_.start(timing_.maxWaitMs);
    }
}

void ProjectAutosaveController::stopDebounceTimers()
{
    debounceTimer_.stop();
    maxWaitTimer_.stop();
}

void ProjectAutosaveController::scheduleRetry()
{
    if (retryAttempt_ >= timing_.retryDelaysMs.size()) {
        return;
    }

    const int delay =
        timing_.retryDelaysMs[retryAttempt_++];
    retryTimer_.start(delay);
}

ProjectAutosaveFlushResult
ProjectAutosaveController::attemptSave()
{
    if (conflictBlocked_) {
        return ProjectAutosaveFlushResult::Conflict;
    }

    if (!pending_ || !session_->isDirty()) {
        pending_ = false;
        retryAttempt_ = 0;
        stopDebounceTimers();
        return ProjectAutosaveFlushResult::Idle;
    }

    stopDebounceTimers();

    int expectedRevision = 0;
    if (session_->state().activeProject) {
        expectedRevision =
            session_->state().activeProject->metadata.revision;
    }

    emit saveStarted(expectedRevision);

    try {
        const projects::ProjectRecord saved =
            saveInvoker_();

        pending_ = false;
        retryAttempt_ = 0;
        retryTimer_.stop();

        emit saveSucceeded(saved.metadata.revision);
        return ProjectAutosaveFlushResult::Saved;
    } catch (const projects::ProjectRepositoryError& error) {
        if (
            error.code()
            == projects::ProjectRepositoryErrorCode::
                ProjectRevisionConflict
        ) {
            conflictBlocked_ = true;
            retryTimer_.stop();
            emit saveFailed(
                QString::fromUtf8(error.what()),
                true
            );
            return ProjectAutosaveFlushResult::Conflict;
        }

        if (
            error.code()
                == projects::ProjectRepositoryErrorCode::DatabaseError
            && retryAttempt_ < timing_.retryDelaysMs.size()
        ) {
            scheduleRetry();
            return ProjectAutosaveFlushResult::RetryScheduled;
        }

        emit saveFailed(
            QString::fromUtf8(error.what()),
            false
        );
        return ProjectAutosaveFlushResult::Error;
    } catch (const std::exception& error) {
        emit saveFailed(
            QString::fromUtf8(error.what()),
            false
        );
        return ProjectAutosaveFlushResult::Error;
    }
}

} // namespace tcgprint::application
