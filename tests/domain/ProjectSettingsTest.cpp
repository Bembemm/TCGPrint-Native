#include <QtTest>

#include "domain/projects/ProjectSettings.h"

#include <limits>

using namespace tcgprint::projects;
using tcgprint::geometry::Millimeters;

class ProjectSettingsTest final : public QObject
{
    Q_OBJECT

private slots:

    void defaultsMatchCurrentPrintContract()
    {
        const ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        QCOMPARE(settings.bleed.value(), 0.625);
        QCOMPARE(
            QString::fromStdString(settings.paperFormat.name),
            QStringLiteral("A4")
        );
        QCOMPARE(settings.paperFormat.width.value(), 210.0);
        QCOMPARE(settings.paperFormat.height.value(), 297.0);
        QCOMPARE(
            QString::fromStdString(settings.cardFormat.id),
            QStringLiteral("magic-standard")
        );
        QCOMPARE(settings.cardFormat.trimWidth.value(), 63.5);
        QCOMPARE(settings.cardFormat.trimHeight.value(), 88.9);
        QCOMPARE(settings.cardFormat.cornerRadius.value(), 3.175);
        QCOMPARE(settings.cutGuides.trim.enabled, false);
        QCOMPARE(settings.cutGuides.trim.fullExtent, false);
        QCOMPARE(settings.cutGuides.trim.extent.value(), 1.0);
        QCOMPARE(settings.cutGuides.trim.color, GuideColor::Blue);
        QCOMPARE(settings.cutGuides.external.enabled, false);
        QCOMPARE(settings.cutGuides.external.strokeWidthPt, 0.3);
        QCOMPARE(settings.cutGuides.external.color, GuideColor::Black);
        QCOMPARE(
            settings.exportContentMode,
            ExportContentMode::FrontOnly
        );
        QCOMPARE(
            settings.printerDuplexMode,
            PrinterDuplexMode::SingleSided
        );

        validateProjectPrintSettings(settings);
    }

    void acceptsBoundedFixedGridAndSkippedSlots()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.layout.rows = 3;
        settings.layout.columns = 3;
        settings.layout.skippedSlotIndices = {0, 8};

        validateProjectPrintSettings(settings);
    }

    void acceptsSkippedSlotsWithTemplateGeometry()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.layout.hasTemplateGeometry = true;
        settings.layout.skippedSlotIndices = {2, 7};

        validateProjectPrintSettings(settings);
    }

    void rejectsInvalidBleedAndNaN()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.bleed = Millimeters(3.01);
        QVERIFY_EXCEPTION_THROWN(
            validateProjectPrintSettings(settings),
            ProjectSettingsError
        );

        settings = ProjectPrintSettings::defaults();
        settings.bleed = Millimeters(
            std::numeric_limits<double>::quiet_NaN()
        );
        QVERIFY_EXCEPTION_THROWN(
            validateProjectPrintSettings(settings),
            ProjectSettingsError
        );
    }

    void rejectsInvalidCutGuideGeometry()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.cutGuides.external.strokeWidthPt = 0.0;

        try {
            validateProjectPrintSettings(settings);
            QFAIL("Expected cut guide validation failure.");
        } catch (const ProjectSettingsError& error) {
            QCOMPARE(
                error.code(),
                ProjectSettingsErrorCode::InvalidCutGuides
            );
        }
    }

    void rejectsCornerRadiusPastHalfSmallerDimension()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.cardFormat.cornerRadius =
            Millimeters(40.0);

        try {
            validateProjectPrintSettings(settings);
            QFAIL("Expected card format validation failure.");
        } catch (const ProjectSettingsError& error) {
            QCOMPARE(
                error.code(),
                ProjectSettingsErrorCode::InvalidCardFormat
            );
        }
    }

    void rejectsBrokenGridAndDuplicateSkips()
    {
        ProjectPrintSettings settings =
            ProjectPrintSettings::defaults();

        settings.layout.rows = 3;
        settings.layout.skippedSlotIndices = {0};

        QVERIFY_EXCEPTION_THROWN(
            validateProjectPrintSettings(settings),
            ProjectSettingsError
        );

        settings = ProjectPrintSettings::defaults();
        settings.layout.rows = 3;
        settings.layout.columns = 3;
        settings.layout.skippedSlotIndices = {1, 1};

        QVERIFY_EXCEPTION_THROWN(
            validateProjectPrintSettings(settings),
            ProjectSettingsError
        );

        settings.layout.skippedSlotIndices = {9};

        QVERIFY_EXCEPTION_THROWN(
            validateProjectPrintSettings(settings),
            ProjectSettingsError
        );
    }
};

QTEST_APPLESS_MAIN(ProjectSettingsTest)

#include "ProjectSettingsTest.moc"
