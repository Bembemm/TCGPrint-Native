#include <QtTest>

#include "persistence/projects/ProjectSnapshot.h"

using namespace tcgprint::projects;

class ProjectSnapshotTest final : public QObject
{
    Q_OBJECT

private slots:

    void readsV6AndPreservesDurableRawFields()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 6,
          "cards": [
            {
              "id": "card-a",
              "quantity": 2,
              "order": 0,
              "identity": {"id": "abc"},
              "backMode": "manual"
            },
            {
              "id": "card-b",
              "quantity": 1,
              "order": 1,
              "selectedArtworkByFace": {"front": {"candidateId": "x"}}
            }
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "exportContentMode": "duplex"
          },
          "physicalOrder": {
            "nextInstanceId": 4,
            "instances": [
              {"id": "instance-1", "workingCardId": "card-a"},
              {"id": "instance-3", "workingCardId": "card-b"},
              {"id": "instance-2", "workingCardId": "card-a"}
            ]
          }
        })JSON";

        const ProjectSnapshotCompat snapshot =
            deserializeProjectSnapshot(json);

        QCOMPARE(snapshot.sourceSchemaVersion, 6);
        QCOMPARE(snapshot.projectSchemaVersion, 6);
        QCOMPARE(snapshot.cards.size(), std::size_t{2});
        QCOMPARE(
            snapshot.cards[0].raw.value(QStringLiteral("backMode")).toString(),
            QStringLiteral("manual")
        );
        QVERIFY(
            snapshot.cards[1].raw.contains(
                QStringLiteral("selectedArtworkByFace")
            )
        );
        QCOMPARE(
            snapshot.settings.value(QStringLiteral("exportContentMode")).toString(),
            QStringLiteral("duplex")
        );

        QCOMPARE(snapshot.physicalOrder.instances.size(), std::size_t{3});
        QCOMPARE(snapshot.physicalOrder.instances[0].id, std::uint64_t{1});
        QCOMPARE(snapshot.physicalOrder.instances[1].id, std::uint64_t{3});
        QCOMPARE(snapshot.physicalOrder.instances[2].id, std::uint64_t{2});
    }

    void promotesV1ToCurrentAndDerivesPhysicalOrder()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 1,
          "cards": [
            {"id": "later", "quantity": 1, "order": 20},
            {"id": "first", "quantity": 2, "order": 10}
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "cutGuides": {}
          }
        })JSON";

        const ProjectSnapshotCompat snapshot =
            deserializeProjectSnapshot(json);

        QCOMPARE(snapshot.sourceSchemaVersion, 1);
        QCOMPARE(snapshot.projectSchemaVersion, 6);
        QCOMPARE(snapshot.physicalOrder.nextInstanceId, std::uint64_t{4});
        QCOMPARE(
            QString::fromStdString(
                snapshot.physicalOrder.instances[0].workingCardId
            ),
            QStringLiteral("first")
        );
        QCOMPARE(
            QString::fromStdString(
                snapshot.physicalOrder.instances[2].workingCardId
            ),
            QStringLiteral("later")
        );
    }

    void rejectsFutureSchema()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 7,
          "cards": [],
          "settings": {},
          "physicalOrder": {"nextInstanceId": 1, "instances": []}
        })JSON";

        try {
            static_cast<void>(deserializeProjectSnapshot(json));
            QFAIL("Expected future schema rejection.");
        } catch (const ProjectSnapshotError& error) {
            QCOMPARE(
                error.code(),
                ProjectSnapshotErrorCode::FutureProjectSchemaVersion
            );
        }
    }

    void rejectsPhysicalOrderThatDoesNotMatchQuantities()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 6,
          "cards": [
            {"id": "card-a", "quantity": 2, "order": 0}
          ],
          "settings": {},
          "physicalOrder": {
            "nextInstanceId": 2,
            "instances": [
              {"id": "instance-1", "workingCardId": "card-a"}
            ]
          }
        })JSON";

        try {
            static_cast<void>(deserializeProjectSnapshot(json));
            QFAIL("Expected physical order validation failure.");
        } catch (const ProjectSnapshotError& error) {
            QCOMPARE(
                error.code(),
                ProjectSnapshotErrorCode::InvalidProjectSnapshot
            );
        }
    }

    void rejectsDuplicateWorkingCardIds()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 5,
          "cards": [
            {"id": "same", "quantity": 1, "order": 0},
            {"id": "same", "quantity": 1, "order": 1}
          ],
          "settings": {}
        })JSON";

        QVERIFY_EXCEPTION_THROWN(
            deserializeProjectSnapshot(json),
            ProjectSnapshotError
        );
    }
};

QTEST_APPLESS_MAIN(ProjectSnapshotTest)

#include "ProjectSnapshotTest.moc"
