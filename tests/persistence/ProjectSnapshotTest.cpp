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

    void derivesLegacySimpleCardBackMode()
    {
        const QByteArray legacy = R"JSON({
          "projectSchemaVersion": 3,
          "cards": [
            {
              "id": "card-a",
              "quantity": 1,
              "order": 0,
              "selectedArtworkByFace": {}
            }
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "cutGuides": {}
          }
        })JSON";

        const ProjectSnapshotCompat snapshot =
            deserializeProjectSnapshot(legacy);

        QCOMPARE(snapshot.cards.size(), std::size_t{1});
        QCOMPARE(
            snapshot.cards[0].backMode,
            tcgprint::cards::BackMode::ProjectDefault
        );
        QCOMPARE(
            snapshot.cards[0].backModeSelectionPolicy,
            tcgprint::cards::BackModeSelectionPolicy::Automatic
        );
        QCOMPARE(
            snapshot.cards[0].raw
                .value(QStringLiteral("backMode"))
                .toString(),
            QStringLiteral("project-default")
        );
    }

    void derivesLegacyDfcBackModeFromProviderMetadata()
    {
        const QByteArray legacy = R"JSON({
          "projectSchemaVersion": 3,
          "cards": [
            {
              "id": "dfc",
              "quantity": 1,
              "order": 0,
              "identity": {
                "metadata": {
                  "layout": "transform",
                  "faces": [
                    {"name": "Front"},
                    {"name": "Back"}
                  ]
                }
              },
              "selectedArtworkByFace": {}
            }
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "cutGuides": {}
          }
        })JSON";

        const ProjectSnapshotCompat snapshot =
            deserializeProjectSnapshot(legacy);

        QCOMPARE(
            snapshot.cards[0].backMode,
            tcgprint::cards::BackMode::Auto
        );
        QCOMPARE(
            snapshot.cards[0].raw
                .value(QStringLiteral("backMode"))
                .toString(),
            QStringLiteral("auto")
        );
    }

    void rejectsCardFieldsThatDidNotExistInSchema()
    {
        const QByteArray invalidLegacy = R"JSON({
          "projectSchemaVersion": 3,
          "cards": [
            {
              "id": "card-a",
              "quantity": 1,
              "order": 0,
              "backMode": "none"
            }
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "cutGuides": {}
          }
        })JSON";

        QVERIFY_EXCEPTION_THROWN(
            deserializeProjectSnapshot(invalidLegacy),
            ProjectSnapshotError
        );
    }

    void serializesCanonicalV6WithoutLosingDurableState()
    {
        const QByteArray json = R"JSON({
          "projectSchemaVersion": 6,
          "cards": [
            {
              "id": "card-a",
              "quantity": 1,
              "order": 0,
              "section": "Main",
              "identity": {
                "id": "oracle-a",
                "provider": "scryfall",
                "name": "Card A",
                "metadata": {"layout": "transform"}
              },
              "selectedArtworkByFace": {
                "front": {
                  "candidateId": "scryfall:front",
                  "source": "scryfall",
                  "identityId": "oracle-a",
                  "faceId": "front"
                }
              },
              "backMode": "manual",
              "manualBackAsset": {
                "assetId": "back-1",
                "sha256": "abc",
                "format": "png"
              },
              "mpcReferences": [
                {
                  "faceId": "front",
                  "importedAssetId": "mpc-1",
                  "slots": ["1"],
                  "availableLocally": true
                }
              ]
            }
          ],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": true,
            "exportContentMode": "duplex",
            "missingBackPolicy": "block",
            "duplexFlipMode": "long-edge",
            "layout": {"skippedSlotIndices": [2, 7]},
            "printerProfileSelection": {
              "profileId": "printer-a",
              "revision": 3
            }
          },
          "physicalOrder": {
            "nextInstanceId": 2,
            "instances": [
              {"id": "instance-1", "workingCardId": "card-a"}
            ]
          }
        })JSON";

        const ProjectSnapshotCompat first =
            deserializeProjectSnapshot(json);
        const QByteArray serialized =
            serializeProjectSnapshot(first);
        const ProjectSnapshotCompat second =
            deserializeProjectSnapshot(serialized);

        QCOMPARE(second.sourceSchemaVersion, 6);
        QCOMPARE(second.projectSchemaVersion, 6);
        QCOMPARE(second.cards.size(), std::size_t{1});
        QCOMPARE(
            second.cards[0].raw.value(QStringLiteral("section")).toString(),
            QStringLiteral("Main")
        );
        QVERIFY(
            second.cards[0].raw
                .value(QStringLiteral("identity"))
                .toObject()
                .value(QStringLiteral("metadata"))
                .toObject()
                .contains(QStringLiteral("layout"))
        );
        QVERIFY(
            second.cards[0].raw.contains(
                QStringLiteral("manualBackAsset")
            )
        );
        QVERIFY(
            second.cards[0].raw.contains(
                QStringLiteral("mpcReferences")
            )
        );
        QCOMPARE(
            second.settings.value(QStringLiteral("missingBackPolicy")).toString(),
            QStringLiteral("block")
        );
        QCOMPARE(
            second.settings
                .value(QStringLiteral("layout"))
                .toObject()
                .value(QStringLiteral("skippedSlotIndices"))
                .toArray()
                .size(),
            2
        );
        QVERIFY(
            second.settings.contains(
                QStringLiteral("printerProfileSelection")
            )
        );
        QCOMPARE(second.physicalOrder.nextInstanceId, std::uint64_t{2});
        QCOMPARE(second.physicalOrder.instances[0].id, std::uint64_t{1});
    }

    void promotesLegacySnapshotToSerializedV6()
    {
        const QByteArray legacy = R"JSON({
          "projectSchemaVersion": 1,
          "cards": [
            {
              "id": "card-a",
              "quantity": 2,
              "order": 0,
              "identity": {"id": "keep-me"},
              "selectedArtworkByFace": {"front": {"candidateId": "keep-me-too"}}
            }
          ],
          "settings": {
            "bleedMm": 0.75,
            "roundedCorners": true,
            "cutGuides": {
              "trim": {"enabled": true},
              "external": {"enabled": false}
            }
          }
        })JSON";

        const ProjectSnapshotCompat migrated =
            deserializeProjectSnapshot(legacy);
        const QByteArray serialized =
            serializeProjectSnapshot(migrated);

        QJsonParseError error;
        const QJsonDocument document =
            QJsonDocument::fromJson(serialized, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(document.isObject());

        const QJsonObject root = document.object();
        QCOMPARE(
            root.value(QStringLiteral("projectSchemaVersion")).toInt(),
            6
        );
        QVERIFY(root.contains(QStringLiteral("physicalOrder")));

        const QJsonObject settings =
            root.value(QStringLiteral("settings")).toObject();

        QCOMPARE(
            settings.value(QStringLiteral("bleedMm")).toDouble(),
            0.75
        );
        QCOMPARE(
            settings.value(QStringLiteral("roundedCorners")).toBool(),
            true
        );
        QCOMPARE(
            settings.value(QStringLiteral("pageOrientation")).toString(),
            QStringLiteral("portrait")
        );
        QCOMPARE(
            settings.value(QStringLiteral("exportContentMode")).toString(),
            QStringLiteral("front-only")
        );
        QCOMPARE(
            settings.value(QStringLiteral("printerDuplexMode")).toString(),
            QStringLiteral("single-sided")
        );
        QVERIFY(
            settings.value(QStringLiteral("cutSourceSelection")).isNull()
        );

        const QJsonObject trim = settings
            .value(QStringLiteral("cutGuides"))
            .toObject()
            .value(QStringLiteral("trim"))
            .toObject();

        QCOMPARE(trim.value(QStringLiteral("enabled")).toBool(), true);
        QCOMPARE(trim.value(QStringLiteral("extentMm")).toDouble(), 1.0);
        QCOMPARE(
            trim.value(QStringLiteral("color")).toString(),
            QStringLiteral("blue")
        );

        const ProjectSnapshotCompat reread =
            deserializeProjectSnapshot(serialized);
        QCOMPARE(reread.sourceSchemaVersion, 6);
        QCOMPARE(reread.physicalOrder.instances.size(), std::size_t{2});
        QVERIFY(
            reread.cards[0].raw.contains(
                QStringLiteral("selectedArtworkByFace")
            )
        );
    }

    void rejectsSettingsFieldsThatDidNotExistInSchema()
    {
        const QByteArray invalidLegacy = R"JSON({
          "projectSchemaVersion": 1,
          "cards": [],
          "settings": {
            "bleedMm": 0.625,
            "roundedCorners": false,
            "cutGuides": {},
            "pageOrientation": "portrait"
          }
        })JSON";

        QVERIFY_EXCEPTION_THROWN(
            deserializeProjectSnapshot(invalidLegacy),
            ProjectSnapshotError
        );
    }

    void serializerRejectsBrokenPhysicalOrder()
    {
        const ProjectSnapshotCompat valid =
            deserializeProjectSnapshot(R"JSON({
              "projectSchemaVersion": 6,
              "cards": [
                {"id": "card-a", "quantity": 1, "order": 0}
              ],
              "settings": {},
              "physicalOrder": {
                "nextInstanceId": 2,
                "instances": [
                  {"id": "instance-1", "workingCardId": "card-a"}
                ]
              }
            })JSON");

        ProjectSnapshotCompat broken = valid;
        broken.physicalOrder.instances.clear();

        QVERIFY_EXCEPTION_THROWN(
            serializeProjectSnapshot(broken),
            ProjectSnapshotError
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
