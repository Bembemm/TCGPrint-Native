#include <QtTest>

#include "persistence/projects/ProjectDatabase.h"
#include "persistence/projects/ProjectRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUuid>

using namespace tcgprint::projects;

namespace {

ProjectSnapshotCompat snapshot()
{
    return deserializeProjectSnapshot(R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {"id": "card-a", "quantity": 1, "order": 0}
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false
      },
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [
          {"id": "instance-1", "workingCardId": "card-a"}
        ]
      }
    })JSON");
}

QString uniqueConnection(const QString& prefix)
{
    return prefix
        + QStringLiteral("-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

class ProjectDatabaseTest final : public QObject
{
    Q_OBJECT

private slots:

    void resolvesCompatibilityPath()
    {
        const QString path =
            projectDatabasePath(QStringLiteral("/tmp/tcgprint-data"));

        QVERIFY(path.endsWith(
            QStringLiteral("/tmp/tcgprint-data/.tcgprint/projects.sqlite")
        ));
    }

    void persistsProjectAcrossDatabaseReopen()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = projectDatabasePath(directory.path());
        std::string projectId;

        {
            QSqlDatabase database = openProjectDatabase(
                path,
                uniqueConnection(QStringLiteral("write"))
            );
            ProjectRepository repository(database);

            const ProjectRecord created =
                repository.create(snapshot(), "Disk Project");
            projectId = created.metadata.id;

            closeProjectDatabase(database);
        }

        QVERIFY(QFileInfo::exists(path));

        {
            QSqlDatabase database = openProjectDatabase(
                path,
                uniqueConnection(QStringLiteral("read"))
            );
            ProjectRepository repository(database);

            const ProjectRecord reopened = repository.open(projectId);
            QCOMPARE(
                QString::fromStdString(reopened.metadata.name),
                QStringLiteral("Disk Project")
            );
            QCOMPARE(reopened.metadata.revision, 1);
            QCOMPARE(reopened.snapshot.cards.size(), std::size_t{1});

            closeProjectDatabase(database);
        }
    }
};

QTEST_GUILESS_MAIN(ProjectDatabaseTest)

#include "ProjectDatabaseTest.moc"
