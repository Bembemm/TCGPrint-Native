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

ProjectSnapshotCompat sampleSnapshot(double bleedMm = 0.625)
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
              "candidateId": "custom:front",
              "source": "custom",
              "identityId": null,
              "faceId": "front"
            }
          }
        }
      ],
      "settings": {
        "bleedMm": %1,
        "roundedCorners": false
      },
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [
          {"id": "instance-1", "workingCardId": "card-a"}
        ]
      }
    })JSON")
        .arg(bleedMm, 0, 'f', 3)
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
            reopened.snapshot.settings.value(QStringLiteral("bleedMm")).toDouble(),
            0.625
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
            sampleSnapshot(0.750)
        );

        QCOMPARE(saved.metadata.revision, 2);
        QCOMPARE(
            saved.snapshot.settings.value(QStringLiteral("bleedMm")).toDouble(),
            0.750
        );

        try {
            static_cast<void>(
                repository.save(
                    created.metadata.id,
                    1,
                    sampleSnapshot(0.875)
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
                sampleSnapshot(1.000)
            );

        QCOMPARE(recovery.baseRevision, 1);

        const auto reread =
            repository.readRecovery(created.metadata.id);
        QVERIFY(reread.has_value());
        QCOMPARE(
            reread->snapshot.settings.value(QStringLiteral("bleedMm")).toDouble(),
            1.000
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
                sampleSnapshot(0.750)
            )
        );

        try {
            static_cast<void>(
                repository.stageRecovery(
                    created.metadata.id,
                    1,
                    sampleSnapshot(1.125)
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

    void duplicatesProjectsWithFreshIdentity()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord original =
            repository.create(sampleSnapshot(), "Original");
        const ProjectRecord duplicate =
            repository.duplicate(original.metadata.id);

        QVERIFY(duplicate.metadata.id != original.metadata.id);
        QCOMPARE(duplicate.metadata.revision, 1);
        QCOMPARE(
            QString::fromStdString(duplicate.metadata.name),
            QStringLiteral("Original (cópia)")
        );
        QCOMPARE(
            duplicate.snapshot.settings.value(QStringLiteral("bleedMm")).toDouble(),
            0.625
        );
        QCOMPARE(repository.list().size(), std::size_t{2});
    }

    void deleteCascadesStagedRecovery()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord created =
            repository.create(sampleSnapshot());
        static_cast<void>(
            repository.stageRecovery(
                created.metadata.id,
                1,
                sampleSnapshot(1.250)
            )
        );

        repository.remove(created.metadata.id);
        QVERIFY(!repository.get(created.metadata.id).has_value());

        QSqlQuery count(storage.database);
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM project_recovery"
        )));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 0);
    }

    void copiesRecoveryToFreshProjectAndClearsCandidate()
    {
        TestDatabase storage;
        ProjectRepository repository(storage.database);

        const ProjectRecord source =
            repository.create(sampleSnapshot(), "Source");
        static_cast<void>(
            repository.stageRecovery(
                source.metadata.id,
                1,
                sampleSnapshot(1.375)
            )
        );

        const ProjectRecord copy =
            repository.copyRecovery(source.metadata.id);

        QVERIFY(copy.metadata.id != source.metadata.id);
        QCOMPARE(copy.metadata.revision, 1);
        QCOMPARE(
            QString::fromStdString(copy.metadata.name),
            QStringLiteral("Source (recuperado)")
        );
        QCOMPARE(
            copy.snapshot.settings.value(QStringLiteral("bleedMm")).toDouble(),
            1.375
        );
        QVERIFY(!repository.readRecovery(source.metadata.id).has_value());
        QCOMPARE(repository.list().size(), std::size_t{2});
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

QTEST_GUILESS_MAIN(ProjectRepositoryTest)

#include "ProjectRepositoryTest.moc"
