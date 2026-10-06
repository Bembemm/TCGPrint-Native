#include <QtTest>

#include "application/ProjectSession.h"

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
            QStringLiteral("tcgprint-session-test-")
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

} // namespace

class ProjectSessionTest final : public QObject
{
    Q_OBJECT

private slots:

    void opensCleanAndMarksEditsDirty()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        const ProjectSessionState& opened =
            session.open(created.metadata.id);

        QCOMPARE(opened.status, ProjectSaveStatus::Saved);
        QVERIFY(!session.isDirty());

        session.replaceSnapshot(snapshot(0.750));

        QCOMPARE(
            session.state().status,
            ProjectSaveStatus::Dirty
        );
        QVERIFY(session.isDirty());
    }

    void savesThroughRecoveryAndAdvancesRevision()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);
        session.replaceSnapshot(snapshot(0.750));

        const ProjectRecord saved = session.save();

        QCOMPARE(saved.metadata.revision, 2);
        QCOMPARE(
            saved.snapshot.printSettings.bleed.value(),
            0.750
        );
        QCOMPARE(
            session.state().status,
            ProjectSaveStatus::Saved
        );
        QVERIFY(!session.isDirty());
        QVERIFY(
            !repository.readRecovery(
                created.metadata.id
            ).has_value()
        );
    }

    void reportsConflictInsteadOfOverwritingNewerRevision()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        ProjectSession session(repository);
        session.open(created.metadata.id);
        session.replaceSnapshot(snapshot(0.750));

        static_cast<void>(
            repository.save(
                created.metadata.id,
                1,
                snapshot(0.875)
            )
        );

        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(session.save()),
            ProjectRepositoryError
        );

        QCOMPARE(
            session.state().status,
            ProjectSaveStatus::Conflict
        );

        const ProjectRecord canonical =
            repository.open(created.metadata.id);
        QCOMPARE(canonical.metadata.revision, 2);
        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            0.875
        );
    }

    void restoresCurrentRecovery()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        static_cast<void>(
            repository.stageRecovery(
                created.metadata.id,
                1,
                snapshot(1.000)
            )
        );

        ProjectSession session(repository);
        session.open(created.metadata.id);

        QVERIFY(session.state().recovery.has_value());

        const ProjectRecord restored =
            session.resolveRecovery(
                ProjectRecoveryChoice::Restore
            );

        QCOMPARE(restored.metadata.revision, 2);
        QCOMPARE(
            restored.snapshot.printSettings.bleed.value(),
            1.000
        );
        QVERIFY(!session.state().recovery.has_value());
    }

    void copiesStaleRecoveryWithoutOverwritingCanonical()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        static_cast<void>(
            repository.stageRecovery(
                created.metadata.id,
                1,
                snapshot(1.000)
            )
        );

        static_cast<void>(
            repository.save(
                created.metadata.id,
                1,
                snapshot(0.875)
            )
        );

        ProjectSession session(repository);
        session.open(created.metadata.id);

        QVERIFY(session.state().recovery.has_value());
        QCOMPARE(
            session.state().recovery->baseRevision,
            1
        );
        QCOMPARE(
            session.state().activeProject->metadata.revision,
            2
        );

        const ProjectRecord copy =
            session.resolveRecovery(
                ProjectRecoveryChoice::Copy
            );

        QVERIFY(copy.metadata.id != created.metadata.id);
        QCOMPARE(copy.metadata.revision, 1);
        QCOMPARE(
            copy.snapshot.printSettings.bleed.value(),
            1.000
        );

        const ProjectRecord canonical =
            repository.open(created.metadata.id);
        QCOMPARE(canonical.metadata.revision, 2);
        QCOMPARE(
            canonical.snapshot.printSettings.bleed.value(),
            0.875
        );
        QVERIFY(
            !repository.readRecovery(
                created.metadata.id
            ).has_value()
        );
    }

    void rejectsInvalidRecoveryDecision()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);
        const ProjectRecord created =
            repository.create(snapshot(), "Project");

        static_cast<void>(
            repository.stageRecovery(
                created.metadata.id,
                1,
                snapshot(1.000)
            )
        );

        ProjectSession session(repository);
        session.open(created.metadata.id);

        try {
            static_cast<void>(
                session.resolveRecovery(
                    ProjectRecoveryChoice::Copy
                )
            );
            QFAIL("Expected invalid current-recovery choice.");
        } catch (const ProjectSessionError& error) {
            QCOMPARE(
                error.code(),
                ProjectSessionErrorCode::InvalidRecoveryChoice
            );
        }
    }
};

QTEST_GUILESS_MAIN(ProjectSessionTest)

#include "ProjectSessionTest.moc"
