#include <QtTest>

#include "persistence/projects/ProjectRepository.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>

using namespace tcgprint::projects;

namespace {

struct TestDatabase final
{
    QString connectionName;
    QSqlDatabase database;

    TestDatabase()
        : connectionName(
            QStringLiteral("tcgprint-test-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces)
          ),
          database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName))
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

ProjectSnapshotCompat sampleSnapshot(QString marker = QStringLiteral("initial"))
{
    const QByteArray json = QString(R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0,
          "selectedArtworkByFace": {
            "front": {
              "candidateId": "%1",
              "source": "custom",
              "identityId": null,
              "faceId": "front"
            }
          }
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "marker": "%1"
      },
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [
          {"id": "instance-1", "workingCardId": "card-a"}
        ]
      }
    })JSON")
        .arg(marker)
        .toUtf8();

    return deserializeProjectSnapshot(json);
}

} // namespace

class ProjectRepositoryTest final : public QObject
{
    Q_OBJECT

private slots:

    void createsListsAndOpensProjects()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord created =
            repository.create(sampleSnapshot(), "Native Project");

        QCOMPARE(created.metadata.revision, 1);
        QCOMPARE(
            QString::fromStdString(created.metadata.name),
            QStringLiteral("Native Project")
        );

        const auto list = repository.list();
        QCOMPARE(list.size(), std::size_t{1});
        QCOMPARE(list[0].id, created.metadata.id);

        const ProjectRecord reopened =
            repository.open(created.metadata.id);
        QCOMPARE(reopened.metadata.revision, 1);
        QCOMPARE(
            reopened.snapshot.settings.value(QStringLiteral("marker")).toString(),
            QStringLiteral("initial")
        );
    }

    void savesWithCompareAndSwapRevision()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord created =
            repository.create(sampleSnapshot());

        const ProjectRecord saved = repository.save(
            created.metadata.id,
            1,
            sampleSnapshot(QStringLiteral("saved"))
        );

        QCOMPARE(saved.metadata.revision, 2);
        QCOMPARE(
            saved.snapshot.settings.value(QStringLiteral("marker")).toString(),
            QStringLiteral("saved")
        );

        try {
            static_cast<void>(
                repository.save(
                    created.metadata.id,
                    1,
                    sampleSnapshot(QStringLiteral("stale"))
                )
            );
            QFAIL("Expected stale revision conflict.");
        } catch (const ProjectRepositoryError& error) {
            QCOMPARE(
                error.code(),
                ProjectRepositoryErrorCode::ProjectRevisionConflict
            );
            QCOMPARE(error.expectedRevision().value(), 1);
            QCOMPARE(error.actualRevision().value(), 2);
        }
    }

    void stagesAndPromotesRecovery()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord created =
            repository.create(sampleSnapshot());

        const ProjectRecoveryRecord recovery =
            repository.stageRecovery(
                created.metadata.id,
                1,
                sampleSnapshot(QStringLiteral("recovered"))
            );

        QCOMPARE(recovery.baseRevision, 1);

        const auto reread =
            repository.readRecovery(created.metadata.id);
        QVERIFY(reread.has_value());
        QCOMPARE(
            reread->snapshot.settings.value(QStringLiteral("marker")).toString(),
            QStringLiteral("recovered")
        );

        const ProjectRecord promoted =
            repository.promoteRecovery(created.metadata.id);

        QCOMPARE(promoted.metadata.revision, 2);
        QCOMPARE(
            promoted.snapshot.settings.value(QStringLiteral("marker")).toString(),
            QStringLiteral("recovered")
        );
        QVERIFY(!repository.readRecovery(created.metadata.id).has_value());
    }

    void rejectsRecoveryBasedOnStaleRevision()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord created =
            repository.create(sampleSnapshot());
        static_cast<void>(
            repository.save(
                created.metadata.id,
                1,
                sampleSnapshot(QStringLiteral("saved"))
            )
        );

        try {
            static_cast<void>(
                repository.stageRecovery(
                    created.metadata.id,
                    1,
                    sampleSnapshot(QStringLiteral("stale-recovery"))
                )
            );
            QFAIL("Expected recovery revision conflict.");
        } catch (const ProjectRepositoryError& error) {
            QCOMPARE(
                error.code(),
                ProjectRepositoryErrorCode::ProjectRevisionConflict
            );
            QCOMPARE(error.actualRevision().value(), 2);
        }
    }

    void acceptsLegacySchemaFiveDatabaseWithoutRewritingIt()
    {
        TestDatabase storage;

        {
            QSqlQuery query(storage.database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE projects ("
                "id TEXT PRIMARY KEY NOT NULL,"
                "name TEXT NOT NULL,"
                "project_schema_version INTEGER NOT NULL,"
                "revision INTEGER NOT NULL,"
                "snapshot_json TEXT NOT NULL,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "autosaved_at TEXT NOT NULL)"
            )));
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE project_recovery ("
                "project_id TEXT PRIMARY KEY NOT NULL REFERENCES projects(id) ON DELETE CASCADE,"
                "base_revision INTEGER NOT NULL,"
                "project_schema_version INTEGER NOT NULL,"
                "snapshot_json TEXT NOT NULL,"
                "created_at TEXT NOT NULL)"
            )));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 5")));
        }

        ProjectRepository repository(storage.database);
        static_cast<void>(repository.create(sampleSnapshot()));

        QSqlQuery version(storage.database);
        QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(version.next());
        QCOMPARE(version.value(0).toInt(), 5);
    }
};

QTEST_APPLESS_MAIN(ProjectRepositoryTest)

#include "ProjectRepositoryTest.moc"
