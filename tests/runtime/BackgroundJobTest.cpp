#include <QtTest>

#include "runtime/BackgroundJob.h"

#include <QPromise>
#include <QSignalSpy>
#include <QThread>

using tcgprint::runtime::BackgroundJob;

class BackgroundJobTest final : public QObject
{
    Q_OBJECT

private slots:

    void reportsCompletionAndProgress()
    {
        BackgroundJob job;
        QSignalSpy completed(&job, &BackgroundJob::completed);

        QVERIFY(job.start([](QPromise<void>& promise) {
            promise.setProgressValue(25);
            promise.setProgressValue(75);
            promise.setProgressValue(100);
        }));

        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(!job.running());
        QCOMPARE(job.progress(), 100);
    }

    void supportsCooperativeCancellation()
    {
        BackgroundJob job;
        QSignalSpy canceled(&job, &BackgroundJob::canceled);

        QVERIFY(job.start([](QPromise<void>& promise) {
            for (int step = 0; step <= 1000; ++step) {
                if (promise.isCanceled()) {
                    return;
                }

                if (step % 10 == 0) {
                    promise.setProgressValue(step / 10);
                }

                QThread::msleep(1);
            }
        }));

        QTRY_VERIFY(job.running());
        job.cancel();

        QTRY_COMPARE(canceled.count(), 1);
        QVERIFY(!job.running());
    }

    void rejectsConcurrentStart()
    {
        BackgroundJob job;

        QVERIFY(job.start([](QPromise<void>& promise) {
            while (!promise.isCanceled()) {
                QThread::msleep(1);
            }
        }));

        QVERIFY(!job.start([](QPromise<void>&) {}));

        job.cancel();
        QTRY_VERIFY(!job.running());
    }
};

QTEST_MAIN(BackgroundJobTest)

#include "BackgroundJobTest.moc"
