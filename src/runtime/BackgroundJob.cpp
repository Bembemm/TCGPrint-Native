#include "runtime/BackgroundJob.h"

#include <QtConcurrentRun>

#include <utility>

namespace tcgprint::runtime {

BackgroundJob::BackgroundJob(QObject* parent)
    : QObject(parent)
{
    connect(
        &watcher_,
        &QFutureWatcher<void>::progressValueChanged,
        this,
        [this](int value) {
            if (progress_ == value) {
                return;
            }

            progress_ = value;
            emit progressChanged();
        }
    );

    connect(
        &watcher_,
        &QFutureWatcher<void>::finished,
        this,
        [this] {
            const bool wasCanceled = watcher_.future().isCanceled();

            running_ = false;
            emit runningChanged();

            if (wasCanceled) {
                emit canceled();
            } else {
                progress_ = 100;
                emit progressChanged();
                emit completed();
            }
        }
    );
}

bool BackgroundJob::running() const noexcept
{
    return running_;
}

int BackgroundJob::progress() const noexcept
{
    return progress_;
}

bool BackgroundJob::start(Work work)
{
    if (running_ || !work) {
        return false;
    }

    progress_ = 0;
    emit progressChanged();

    running_ = true;
    emit runningChanged();

    watcher_.setFuture(
        QtConcurrent::run(
            [work = std::move(work)](QPromise<void>& promise) mutable {
                promise.setProgressRange(0, 100);
                work(promise);
            }
        )
    );

    return true;
}

void BackgroundJob::cancel()
{
    if (!running_) {
        return;
    }

    watcher_.future().cancel();
}

} // namespace tcgprint::runtime
