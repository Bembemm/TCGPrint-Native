#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QPromise>

#include <functional>

namespace tcgprint::runtime {

class BackgroundJob final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)

public:
    using Work = std::function<void(QPromise<void>&)>;

    explicit BackgroundJob(QObject* parent = nullptr);

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] int progress() const noexcept;

    bool start(Work work);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void progressChanged();
    void completed();
    void canceled();

private:
    QFutureWatcher<void> watcher_;
    bool running_{false};
    int progress_{0};
};

} // namespace tcgprint::runtime
