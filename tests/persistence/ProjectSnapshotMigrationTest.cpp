#include <QtTest>

#include "persistence/projects/ProjectSnapshot.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace tcgprint::projects;

namespace {

QJsonObject cutGuides()
{
    QJsonObject trim;
    trim.insert(QStringLiteral("enabled"), true);
    trim.insert(QStringLiteral("extentMm"), 1.25);
    trim.insert(QStringLiteral("color"), QStringLiteral("red"));

    QJsonObject external;
    external.insert(QStringLiteral("enabled"), true);
    external.insert(QStringLiteral("strokeWidthPt"), 0.5);
    external.insert(QStringLiteral("color"), QStringLiteral("black"));

    QJsonObject guides;
    guides.insert(QStringLiteral("trim"), trim);
    guides.insert(QStringLiteral("external"), external);
    return guides;
}

QJsonObject card(int schemaVersion)
{
    QJsonObject importSource;
    importSource.insert(QStringLiteral("sourceId"), QStringLiteral("source-a"));
    importSource.insert(QStringLiteral("importKind"), QStringLiteral("text"));
    importSource.insert(QStringLiteral("entryKind"), QStringLiteral("card"));

    QJsonObject hints;
    hints.insert(QStringLiteral("name"), QStringLiteral("Migration Card"));

    QJsonObject resolution;
    resolution.insert(QStringLiteral("status"), QStringLiteral("unresolved"));
    resolution.insert(QStringLiteral("candidates"), QJsonArray{});
    resolution.insert(QStringLiteral("confirmed"), false);

    QJsonObject face;
    face.insert(QStringLiteral("id"), QStringLiteral("front"));
    face.insert(QStringLiteral("side"), QStringLiteral("front"));
    face.insert(QStringLiteral("name"), QStringLiteral("Migration Card"));

    QJsonObject result;
    result.insert(QStringLiteral("id"), QStringLiteral("card-a"));
    result.insert(QStringLiteral("quantity"), 2);
    result.insert(QStringLiteral("order"), 0);
    result.insert(QStringLiteral("importSource"), importSource);
    result.insert(QStringLiteral("identityHints"), hints);
    result.insert(QStringLiteral("identity"), QJsonValue::Null);
    result.insert(QStringLiteral("identityResolution"), resolution);
    result.insert(QStringLiteral("faces"), QJsonArray{face});
    result.insert(QStringLiteral("selectedArtworkByFace"), QJsonObject{});
    result.insert(QStringLiteral("localArtworkIds"), QJsonArray{});
    result.insert(QStringLiteral("mpcReferences"), QJsonArray{});
    result.insert(QStringLiteral("faceAssociations"), QJsonArray{});

    if (schemaVersion >= 4) {
        result.insert(
            QStringLiteral("backMode"),
            QStringLiteral("project-default")
        );
        result.insert(
            QStringLiteral("backModeSelectionPolicy"),
            QStringLiteral("automatic")
        );
    }

    return result;
}

QJsonObject settings(int schemaVersion)
{
    QJsonObject result;
    result.insert(QStringLiteral("bleedMm"), 0.75);
    result.insert(QStringLiteral("roundedCorners"), true);
    result.insert(QStringLiteral("cutGuides"), cutGuides());

    if (schemaVersion >= 2) {
        result.insert(
            QStringLiteral("pageOrientation"),
            QStringLiteral("landscape")
        );
        result.insert(
            QStringLiteral("cardOrientation"),
            QStringLiteral("portrait")
        );

        QJsonObject paper;
        paper.insert(QStringLiteral("name"), QStringLiteral("A4"));
        paper.insert(QStringLiteral("widthMm"), 210.0);
        paper.insert(QStringLiteral("heightMm"), 297.0);
        result.insert(QStringLiteral("paperFormat"), paper);

        QJsonObject cardFormat;
        cardFormat.insert(
            QStringLiteral("id"),
            QStringLiteral("magic-standard")
        );
        cardFormat.insert(
            QStringLiteral("name"),
            QStringLiteral("Magic Standard")
        );
        cardFormat.insert(QStringLiteral("widthMm"), 63.5);
        cardFormat.insert(QStringLiteral("heightMm"), 88.9);
        cardFormat.insert(QStringLiteral("cornerRadiusMm"), 3.175);
        result.insert(QStringLiteral("cardFormat"), cardFormat);

        QJsonObject margins;
        margins.insert(QStringLiteral("top"), 5.0);
        margins.insert(QStringLiteral("right"), 6.0);
        margins.insert(QStringLiteral("bottom"), 7.0);
        margins.insert(QStringLiteral("left"), 8.0);
        result.insert(QStringLiteral("marginsMm"), margins);

        result.insert(QStringLiteral("horizontalGapMm"), 2.0);
        result.insert(QStringLiteral("verticalGapMm"), 3.0);

        QJsonObject registration;
        registration.insert(QStringLiteral("type"), QStringLiteral("none"));
        registration.insert(
            QStringLiteral("orientation"),
            QStringLiteral("portrait")
        );
        result.insert(QStringLiteral("registration"), registration);
        result.insert(QStringLiteral("registrationOverride"), false);

        QJsonObject layout;
        layout.insert(QStringLiteral("rows"), 3);
        layout.insert(QStringLiteral("columns"), 3);
        layout.insert(
            QStringLiteral("skippedSlotIndices"),
            QJsonArray{1, 7}
        );
        result.insert(QStringLiteral("layout"), layout);
    }

    if (schemaVersion >= 3) {
        QJsonObject cutSource;
        cutSource.insert(QStringLiteral("fileId"), QStringLiteral("cut-file"));
        cutSource.insert(
            QStringLiteral("fileHash"),
            QString(64, QLatin1Char('a'))
        );
        result.insert(QStringLiteral("cutSourceSelection"), cutSource);
    }

    if (schemaVersion >= 4) {
        result.insert(
            QStringLiteral("exportContentMode"),
            QStringLiteral("duplex")
        );
        result.insert(
            QStringLiteral("missingBackPolicy"),
            QStringLiteral("block")
        );
        result.insert(
            QStringLiteral("duplexFlipMode"),
            QStringLiteral("short-edge")
        );

        QJsonObject projectDefaultBack;
        const QString sha(64, QLatin1Char('b'));
        projectDefaultBack.insert(
            QStringLiteral("assetId"),
            QStringLiteral("back:") + sha
        );
        projectDefaultBack.insert(QStringLiteral("sha256"), sha);
        projectDefaultBack.insert(QStringLiteral("format"), QStringLiteral("png"));
        result.insert(
            QStringLiteral("projectDefaultBack"),
            projectDefaultBack
        );
    }

    if (schemaVersion >= 5) {
        result.insert(QStringLiteral("printerProfileSelection"), QJsonValue::Null);
        result.insert(
            QStringLiteral("printerDuplexMode"),
            QStringLiteral("automatic-long-edge")
        );
    }

    return result;
}

QByteArray snapshotJson(int schemaVersion)
{
    QJsonObject root;
    root.insert(QStringLiteral("projectSchemaVersion"), schemaVersion);
    root.insert(QStringLiteral("cards"), QJsonArray{card(schemaVersion)});
    root.insert(QStringLiteral("settings"), settings(schemaVersion));

    if (schemaVersion >= 6) {
        QJsonObject first;
        first.insert(QStringLiteral("id"), QStringLiteral("instance-2"));
        first.insert(QStringLiteral("workingCardId"), QStringLiteral("card-a"));

        QJsonObject second;
        second.insert(QStringLiteral("id"), QStringLiteral("instance-1"));
        second.insert(QStringLiteral("workingCardId"), QStringLiteral("card-a"));

        QJsonObject order;
        order.insert(QStringLiteral("nextInstanceId"), 3);
        order.insert(QStringLiteral("instances"), QJsonArray{first, second});
        root.insert(QStringLiteral("physicalOrder"), order);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class ProjectSnapshotMigrationTest final : public QObject
{
    Q_OBJECT

private slots:

    void migratesEverySupportedSchemaToCanonicalV6()
    {
        for (int version = 1; version <= 6; ++version) {
            const ProjectSnapshotCompat migrated =
                deserializeProjectSnapshot(snapshotJson(version));

            QCOMPARE(migrated.sourceSchemaVersion, version);
            QCOMPARE(
                migrated.projectSchemaVersion,
                CurrentProjectSchemaVersion
            );
            QCOMPARE(migrated.cards.size(), std::size_t{1});
            QCOMPARE(
                migrated.physicalOrder.instances.size(),
                std::size_t{2}
            );
            QCOMPARE(migrated.printSettings.bleed.value(), 0.75);
            QCOMPARE(migrated.printSettings.roundedCorners, true);

            if (version >= 2) {
                QCOMPARE(
                    migrated.printSettings.pageOrientation,
                    PageOrientation::Landscape
                );
                QCOMPARE(migrated.printSettings.margins.top.value(), 5.0);
                QCOMPARE(
                    migrated.printSettings.layout.rows.value(),
                    std::size_t{3}
                );
                QCOMPARE(
                    migrated.printSettings.layout.skippedSlotIndices.size(),
                    std::size_t{2}
                );
            } else {
                QCOMPARE(
                    migrated.printSettings.pageOrientation,
                    PageOrientation::Portrait
                );
            }

            if (version >= 4) {
                QCOMPARE(
                    migrated.printSettings.exportContentMode,
                    ExportContentMode::Duplex
                );
                QCOMPARE(
                    migrated.printSettings.missingBackPolicy,
                    MissingBackPolicy::Block
                );
                QCOMPARE(
                    migrated.printSettings.duplexFlipMode,
                    DuplexFlipMode::ShortEdge
                );
                QVERIFY(
                    migrated.settings
                        .value(QStringLiteral("projectDefaultBack"))
                        .isObject()
                );
            } else {
                QCOMPARE(
                    migrated.printSettings.exportContentMode,
                    ExportContentMode::FrontOnly
                );
            }

            if (version >= 5) {
                QCOMPARE(
                    migrated.printSettings.printerDuplexMode,
                    PrinterDuplexMode::AutomaticLongEdge
                );
            } else {
                QCOMPARE(
                    migrated.printSettings.printerDuplexMode,
                    PrinterDuplexMode::SingleSided
                );
            }

            if (version == 6) {
                QCOMPARE(
                    migrated.physicalOrder.instances[0].id,
                    PhysicalInstanceId{2}
                );
                QCOMPARE(
                    migrated.physicalOrder.instances[1].id,
                    PhysicalInstanceId{1}
                );
            } else {
                QCOMPARE(
                    migrated.physicalOrder.instances[0].id,
                    PhysicalInstanceId{1}
                );
                QCOMPARE(
                    migrated.physicalOrder.instances[1].id,
                    PhysicalInstanceId{2}
                );
            }

            const QByteArray serialized =
                serializeProjectSnapshot(migrated);

            const QJsonDocument document =
                QJsonDocument::fromJson(serialized);
            QVERIFY(document.isObject());
            QCOMPARE(
                document.object()
                    .value(QStringLiteral("projectSchemaVersion"))
                    .toInt(),
                CurrentProjectSchemaVersion
            );

            const ProjectSnapshotCompat reread =
                deserializeProjectSnapshot(serialized);
            QCOMPARE(
                reread.sourceSchemaVersion,
                CurrentProjectSchemaVersion
            );
            QCOMPARE(reread.cards.size(), std::size_t{1});
            QCOMPARE(
                reread.physicalOrder,
                migrated.physicalOrder
            );

            if (version >= 3) {
                QVERIFY(
                    reread.settings
                        .value(QStringLiteral("cutSourceSelection"))
                        .isObject()
                );
            }

            if (version >= 5) {
                QCOMPARE(
                    reread.settings
                        .value(QStringLiteral("printerDuplexMode"))
                        .toString(),
                    QStringLiteral("automatic-long-edge")
                );
            }
        }
    }
};

QTEST_APPLESS_MAIN(ProjectSnapshotMigrationTest)

#include "ProjectSnapshotMigrationTest.moc"
