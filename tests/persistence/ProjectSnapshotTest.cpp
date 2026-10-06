#include "ProjectSnapshotTest.h"

#include "persistence/projects/ProjectSnapshot.h"

using namespace tcgprint::projects;

void ProjectSnapshotTest::readsV6AndPreservesDurableRawFields()
{
    const QByteArray json = R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 2,
          "order": 0,
          "importSource": {"sourceId": "source-a", "importKind": "text", "entryKind": "card"},
          "identityHints": {},
          "identity": {"id": "abc"},
          "identityResolution": {"status": "resolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {},
          "backMode": "manual",
          "backModeSelectionPolicy": "explicit",
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        },
        {
          "id": "card-b",
          "quantity": 1,
          "order": 1,
          "importSource": {"sourceId": "source-b", "importKind": "text", "entryKind": "card"},
          "identityHints": {},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Card B"}],
          "selectedArtworkByFace": {"front": {"candidateId": "x"}},
          "backMode": "project-default",
          "backModeSelectionPolicy": "automatic",
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
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

void ProjectSnapshotTest::promotesV1ToCurrentAndDerivesPhysicalOrder()
{
    const QByteArray json = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "later",
          "quantity": 1,
          "order": 20,
          "importSource": {"sourceId": "later-source", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "Later"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Later"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        },
        {
          "id": "first",
          "quantity": 2,
          "order": 10,
          "importSource": {"sourceId": "first-source", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "First"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "First"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        }
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

void ProjectSnapshotTest::derivesLegacySimpleCardBackMode()
{
    const QByteArray legacy = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0,
          "importSource": {"sourceId": "source-a", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "Card A"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        }
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

void ProjectSnapshotTest::derivesLegacyDfcBackModeFromProviderMetadata()
{
    const QByteArray legacy = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "dfc",
          "quantity": 1,
          "order": 0,
          "importSource": {"sourceId": "dfc-source", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "Front // Back"},
          "identity": {
            "id": "scryfall:oracle:dfc",
            "provider": "scryfall",
            "name": "Front // Back",
            "resolutionMethod": "manual",
            "confidence": 1.0,
            "metadata": {
              "layout": "transform",
              "faces": [
                {"name": "Front"},
                {"name": "Back"}
              ]
            }
          },
          "identityResolution": {
            "status": "resolved",
            "method": "manual",
            "candidates": [],
            "confirmed": true
          },
          "faces": [
            {"id": "front", "side": "front", "name": "Front"},
            {"id": "back", "side": "back", "name": "Back"}
          ],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        }
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

void ProjectSnapshotTest::rejectsIncompleteCurrentWorkingCardShape()
{
    const QByteArray invalidCurrent = R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0
        }
      ],
      "settings": {},
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [
          {"id": "instance-1", "workingCardId": "card-a"}
        ]
      }
    })JSON";

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(deserializeProjectSnapshot(invalidCurrent)),
        ProjectSnapshotError
    );
    }

void ProjectSnapshotTest::rejectsCardFieldsThatDidNotExistInSchema()
{
    const QByteArray invalidLegacy = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0,
          "importSource": {"sourceId": "source-a", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "Card A"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": [],
          "backMode": "none"
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        }
      }
    })JSON";

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(deserializeProjectSnapshot(invalidLegacy)),
        ProjectSnapshotError
    );
    }

void ProjectSnapshotTest::serializesCanonicalV6WithoutLosingDurableState()
{
    const QByteArray json = R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 1,
          "order": 0,
          "section": "Main",
          "importSource": {"sourceId": "source-a", "importKind": "text", "entryKind": "card"},
          "identityHints": {},
          "identity": {
            "id": "oracle-a",
            "provider": "scryfall",
            "name": "Card A",
            "metadata": {"layout": "transform"}
          },
          "identityResolution": {"status": "resolved", "candidates": [], "confirmed": true},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {
            "front": {
              "candidateId": "scryfall:front",
              "source": "scryfall",
              "identityId": "oracle-a",
              "faceId": "front"
            }
          },
          "backMode": "manual",
          "backModeSelectionPolicy": "explicit",
          "localArtworkIds": [],
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
          ],
          "faceAssociations": []
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

void ProjectSnapshotTest::promotesLegacySnapshotToSerializedV6()
{
    const QByteArray legacy = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "card-a",
          "quantity": 2,
          "order": 0,
          "importSource": {
            "sourceId": "source-a",
            "importKind": "text",
            "entryKind": "card"
          },
          "identityHints": {"name": "Legacy Card"},
          "identity": null,
          "identityResolution": {
            "status": "unresolved",
            "candidates": [],
            "confirmed": false
          },
          "faces": [
            {"id": "front", "side": "front", "name": "Legacy Card"}
          ],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": 0.75,
        "roundedCorners": true,
        "cutGuides": {
          "trim": {
            "enabled": true,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
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
    QCOMPARE(
        reread.cards[0].raw
            .value(QStringLiteral("backMode"))
            .toString(),
        QStringLiteral("project-default")
    );
    QCOMPARE(
        reread.cards[0].raw
            .value(QStringLiteral("backModeSelectionPolicy"))
            .toString(),
        QStringLiteral("automatic")
    );
    QVERIFY(
        reread.cards[0].raw.contains(
            QStringLiteral("selectedArtworkByFace")
        )
    );
    }

void ProjectSnapshotTest::rejectsSettingsFieldsThatDidNotExistInSchema()
{
    const QByteArray invalidLegacy = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        },
        "pageOrientation": "portrait"
      }
    })JSON";

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(deserializeProjectSnapshot(invalidLegacy)),
        ProjectSnapshotError
    );
    }

void ProjectSnapshotTest::serializerRejectsBrokenPhysicalOrder()
{
    const ProjectSnapshotCompat valid =
        deserializeProjectSnapshot(R"JSON({
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
        static_cast<void>(serializeProjectSnapshot(broken)),
        ProjectSnapshotError
    );
    }

void ProjectSnapshotTest::rejectsFutureSchema()
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

void ProjectSnapshotTest::rejectsPhysicalOrderThatDoesNotMatchQuantities()
{
    const QByteArray json = R"JSON({
      "projectSchemaVersion": 6,
      "cards": [
        {
          "id": "card-a",
          "quantity": 2,
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

void ProjectSnapshotTest::rejectsDuplicateWorkingCardIds()
{
    const QByteArray json = R"JSON({
      "projectSchemaVersion": 1,
      "cards": [
        {
          "id": "same",
          "quantity": 1,
          "order": 0,
          "importSource": {"sourceId": "source-1", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "First"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "First"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        },
        {
          "id": "same",
          "quantity": 1,
          "order": 1,
          "importSource": {"sourceId": "source-2", "importKind": "text", "entryKind": "card"},
          "identityHints": {"name": "Second"},
          "identity": null,
          "identityResolution": {"status": "unresolved", "candidates": [], "confirmed": false},
          "faces": [{"id": "front", "side": "front", "name": "Second"}],
          "selectedArtworkByFace": {},
          "localArtworkIds": [],
          "mpcReferences": [],
          "faceAssociations": []
        }
      ],
      "settings": {
        "bleedMm": 0.625,
        "roundedCorners": false,
        "cutGuides": {
          "trim": {
            "enabled": false,
            "extentMm": 1.0,
            "color": "blue"
          },
          "external": {
            "enabled": false,
            "strokeWidthPt": 0.3,
            "color": "black"
          }
        }
      }
    })JSON";

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(deserializeProjectSnapshot(json)),
        ProjectSnapshotError
    );
    }
QTEST_APPLESS_MAIN(ProjectSnapshotTest)
