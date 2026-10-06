#include <QtTest>
#include <QSignalSpy>

#include "application/ProjectAutosaveController.h"

#include <QSqlDatabase>
#include <QUuid>

using namespace tcgprint::application;
using namespace tcgprint::projects;

namespace {

struct TestDatabase final
{
    QString connectionName;
    QSqlDatabase database;

    TestDatabase()
        : connectionName(
            QStringLiteral("tcgprint-autosave-test-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces)
          ),
          database(
              QSqlDatabase::addDatabase(
                  QStringLiteral("QSQLITE"),
                  connectionName
              )
          )
    {
        database.setDatabaseName(QStringLiteral(":memory:"));
        if (!database.open()) {
            qFatal("Could not open in-memory QSQLITE database.");
        }
    }

    ~TestDatabase()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
};

ProjectSnapshotCompat snapshot(double bleedMm = 0.625)
{
    const QByteArray json = QString(R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0,
          "importSource": {"sourceId": "source-a", "importKind": "text", "entryKind": "card"},
          "identityHints": {},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {},
          "backMode": "project-default",
          "backModeSelectionPolicy": "automatic",
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": %1,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {"enabled": false, "extentMm": 1.0, "color": "blue"},
          "external": {"enabled": false, "strokeWidthPt": 0.3, "color": "black"}
        }
      },
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [{"id": "instance-1", "workingCardId": "card-a"}]
      }
    })JSON")
        .arg(bleedMm, 0, 'f', 3)
        .toUtf8();

    return deserializeProjectSnapshot(json);
}

ProjectAutosaveTiming fastTiming()
{
    return ProjectAutosaveTiming{
        .debounceMs = 10,
        .maxWaitMs = 80,
        .retryDelaysMs = {10, 20, 30},
    };
}

} // namespace

class ProjectAutosaveControllerTest final : public QObject
{
    Q_OBJECT

private slots:

    void debouncesAndSavesLatestObservedSnapshot()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);

        ProjectAutosaveController autosave(
            session,
            fastTiming(),
            {},
            this
        );

        QSignalSpy saved(
            &autosave,
            &ProjectAutosaveController::saveSucceeded
        );

        autosave.observe(snapshot(0.750));
        autosave.observe(snapshot(0.875));

        QTest::qWait(60);

        QCOMPARE(saved.count(), 1);
        QVERIFY(!autosave.hasPendingSave());

        const ProjectRecord canonical =
            repository.open(created.metadata.id);

        QCOMPARE(canonical.metadata.revision, 2);
        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            0.875
        );
    }

    void maxWaitEventuallyFlushesContinuousEdits()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);

        ProjectAutosaveTiming timing{
            .debounceMs = 60,
            .maxWaitMs = 70,
            .retryDelaysMs = {10},
        };

        ProjectAutosaveController autosave(
            session,
            timing,
            {},
            this
        );

        QSignalSpy saved(
            &autosave,
            &ProjectAutosaveController::saveSucceeded
        );

        autosave.observe(snapshot(0.700));
        QTest::qWait(25);
        autosave.observe(snapshot(0.800));
        QTest::qWait(25);
        autosave.observe(snapshot(0.900));
        QTest::qWait(40);

        QCOMPARE(saved.count(), 1);

        const ProjectRecord canonical =
            repository.open(created.metadata.id);

        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            0.900
        );
    }

    void conflictBlocksFurtherAutomaticSaves()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);

        ProjectAutosaveController autosave(
            session,
            fastTiming(),
            {},
            this
        );

        QSignalSpy failed(
            &autosave,
            &ProjectAutosaveController::saveFailed
        );

        autosave.observe(snapshot(0.750));

        static_cast<void>(
            repository.save(
                created.metadata.id,
                1,
                snapshot(1.000)
            )
        );

        QCOMPARE(
            autosave.flushNow(),
            ProjectAutosaveFlushResult::Conflict
        );
        QCOMPARE(failed.count(), 1);
        QVERIFY(autosave.isConflictBlocked());

        autosave.observe(snapshot(1.250));
        QTest::qWait(40);

        QCOMPARE(failed.count(), 1);

        const ProjectRecord canonical =
            repository.open(created.metadata.id);
        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            1.000
        );
    }

    void retriesTransientDatabaseErrors()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);

        int attempts = 0;

        ProjectAutosaveController autosave(
            session,
            fastTiming(),
            [&]() -> ProjectRecord {
                ++attempts;
                if (attempts < 3) {
                    throw ProjectRepositoryError(
                        ProjectRepositoryErrorCode::DatabaseError,
                        "transient database error"
                    );
                }
                return session.autosave();
            },
            this
        );

        QSignalSpy saved(
            &autosave,
            &ProjectAutosaveController::saveSucceeded
        );
        QSignalSpy failed(
            &autosave,
            &ProjectAutosaveController::saveFailed
        );

        autosave.observe(snapshot(0.750));
        QTest::qWait(100);

        QCOMPARE(attempts, 3);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(failed.count(), 0);

        const ProjectRecord canonical =
            repository.open(created.metadata.id);
        QCOMPARE(canonical.metadata.revision, 2);
        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            0.750
        );
    }

    void resetCancelsPendingTimersAndConflictBlock()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);

        ProjectAutosaveController autosave(
            session,
            fastTiming(),
            {},
            this
        );

        QSignalSpy saved(
            &autosave,
            &ProjectAutosaveController::saveSucceeded
        );

        autosave.observe(snapshot(0.750));
        QVERIFY(autosave.hasPendingSave());

        autosave.reset();
        QTest::qWait(40);

        QCOMPARE(saved.count(), 0);
        QVERIFY(!autosave.hasPendingSave());
        QVERIFY(!autosave.isConflictBlocked());

        const ProjectRecord canonical =
            repository.open(created.metadata.id);
        QCOMPARE(canonical.metadata.revision, 1);
    }
};

QTEST_GUILESS_MAIN(ProjectAutosaveControllerTest)

#include "ProjectAutosaveControllerTest.moc"
