#pragma once

#include "application/ProjectSession.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <vector>

namespace tcgprint::application {

inline constexpr int ProjectAutosaveDebounceMs = 600;
inline constexpr int ProjectAutosaveMaxWaitMs = 2000;

struct ProjectAutosaveTiming final
{
    int debounceMs{ProjectAutosaveDebounceMs};
    int maxWaitMs{ProjectAutosaveMaxWaitMs};
    std::vector<int> retryDelaysMs{500, 1000, 2000};
};

enum class ProjectAutosaveFlushResult
{
    Saved,
    Idle,
    RetryScheduled,
    Error,
    Conflict,
};

class ProjectAutosaveController final : public QObject
{
    Q_OBJECT

public:
    using SaveInvoker =
        std::function<projects::ProjectRecord()>;

    explicit ProjectAutosaveController(
        ProjectSession& session,
        QObject* parent = nullptr
    );

    ProjectAutosaveController(
        ProjectSession& session,
        ProjectAutosaveTiming timing,
        SaveInvoker saveInvoker,
        QObject* parent = nullptr
    );

    void observe(
        const projects::ProjectSnapshotCompat& snapshot
    );

    [[nodiscard]] ProjectAutosaveFlushResult flushNow();

    void reset();

    [[nodiscard]] bool hasPendingSave() const noexcept;
    [[nodiscard]] bool isConflictBlocked() const noexcept;

signals:
    void saveStarted(int expectedRevision);
    void saveSucceeded(int revision);
    void saveFailed(const QString& message, bool conflict);

private:
    ProjectSession* session_;
    ProjectAutosaveTiming timing_;
    SaveInvoker saveInvoker_;

    QTimer debounceTimer_;
    QTimer maxWaitTimer_;
    QTimer retryTimer_;

    bool pending_{false};
    bool conflictBlocked_{false};
    std::size_t retryAttempt_{0};

    void schedule();
    void stopDebounceTimers();
    void scheduleRetry();
    [[nodiscard]] ProjectAutosaveFlushResult attemptSave();
};

} // namespace tcgprint::application
