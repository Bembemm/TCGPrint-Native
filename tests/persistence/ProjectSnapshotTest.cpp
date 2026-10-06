        "instances": [{"id": "instance-1", "workingCardId": "card-a"}]
      }
    })JSON";

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(deserializeProjectSnapshot(json)),
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
            "resolutionMethod": "manual",
            "confidence": 1.0,
            "metadata": {"layout": "transform"}
          },
          "identityResolution": {"status": "resolved", "method": "manual", "candidates": [], "confirmed": true},
          "faces": [{"id": "front", "side": "front", "name": "Card A"}],
          "selectedArtworkByFace": {
            "front": {
              "candidateId": "scryfall:bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb:front",
              "source": "scryfall",
              "identityId": "oracle-a",
              "faceId": "front"
            }
          },
          "backMode": "manual",
          "backModeSelectionPolicy": "explicit",
          "localArtworkIds": [],
          "manualBackAsset": {
            "assetId": "back:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
            "sha256": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
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
        "layout": {"rows": 3, "columns": 3, "skippedSlotIndices": [2, 7]},
        "printerProfileSelection": {
          "id": "printer-a",
          "name": "Printer A",
          "front": {
            "offsetXUm": 0,
            "offsetYUm": 0,
            "rotationDeg": 0,
            "scaleX": 1,
            "scaleY": 1
          },
          "back": {
            "offsetXUm": 0,
            "offsetYUm": 0,
            "rotationDeg": 0,
            "scaleX": 1,
            "scaleY": 1
          },
          "paperSize": "A4",
          "paperWidthMm": 210,
          "paperHeightMm": 297,
          "pageOrientation": "portrait",
          "duplexMode": "single-sided",
          "physicalValidationStatus": "software-only",
          "physicalVerification": null,
          "version": 3,
          "profileHash": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
        }
      },
      "physicalOrder": {
        "nextInstanceId": 2,
        "instances": [
          {"id": "instance-1", "workingCardId": "card-a"}
        ]
      }
    })JSON";

    ProjectSnapshotCompat first =
        deserializeProjectSnapshot(json);

    QCOMPARE(first.workingCards.size(), std::size_t{1});
    QVERIFY(first.workingCards[0].identity.has_value());
    QCOMPARE(
        QString::fromStdString(first.workingCards[0].identity->name),
        QStringLiteral("Card A")
    );
    QVERIFY(first.workingCards[0].selectedFrontArtwork.has_value());
    QVERIFY(first.workingCards[0].manualBackAsset.has_value());
    QCOMPARE(first.workingCards[0].mpcReferences.size(), std::size_t{1});

    first.workingCards[0].section = std::string("Typed Main");
    first.workingCards[0].identity->name = "Typed Card A";
    first.workingCards[0].displayName = "Typed Card A";

    const QByteArray serialized =
        serializeProjectSnapshot(first);
    const ProjectSnapshotCompat second =
        deserializeProjectSnapshot(serialized);

    QCOMPARE(second.sourceSchemaVersion, 6);
    QCOMPARE(second.projectSchemaVersion, 6);
    QCOMPARE(second.cards.size(), std::size_t{1});
    QCOMPARE(
        second.cards[0].raw.value(QStringLiteral("section")).toString(),
        QStringLiteral("Typed Main")
    );
    QVERIFY(
        second.cards[0].raw
            .value(QStringLiteral("identity"))
            .toObject()
            .value(QStringLiteral("metadata"))
            .toObject()
            .contains(QStringLiteral("layout"))
    );
    QCOMPARE(second.workingCards.size(), std::size_t{1});
    QVERIFY(second.workingCards[0].identity.has_value());
    QCOMPARE(
        QString::fromStdString(second.workingCards[0].identity->name),
        QStringLiteral("Typed Card A")
    );
    QCOMPARE(
        QString::fromStdString(
            second.workingCards[0].section.value_or("")
        ),
        QStringLiteral("Typed Main")
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

void ProjectSnapshotTest::typedPrintSettingsDriveSerializedV6()
{
    const QByteArray json = R"JSON({
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
        "bleedMm": 0.625,
        "roundedCorners": false,