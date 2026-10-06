#include <QtTest>

#include "import/ImportLimits.h"
#include "import/SourcePath.h"

#include <stdexcept>

using namespace tcgprint::imports;

class ImportBoundaryTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultLimitsMatchWebContract()
    {
        const ImportLimits limits = resolveImportLimits();

        QCOMPARE(limits.maxInputBytes, 50ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxRasterPixels, 100'000'000ULL);
        QCOMPARE(limits.maxSvgBytes, 5ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxTextBytes, 5ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxCsvBytes, 25ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxCsvRows, 100'000ULL);
        QCOMPARE(limits.maxJsonBytes, 8ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxJsonDepth, 64ULL);
        QCOMPARE(limits.maxJsonNodes, 250'000ULL);
        QCOMPARE(limits.maxXmlBytes, 5ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxXmlDepth, 64ULL);
        QCOMPARE(limits.maxXmlNodes, 100'000ULL);
        QCOMPARE(limits.maxZipArchiveBytes, 100ULL * 1024ULL * 1024ULL);
        QCOMPARE(limits.maxZipEntries, 500ULL);
        QCOMPARE(limits.maxZipEntryBytes, 50ULL * 1024ULL * 1024ULL);
        QCOMPARE(
            limits.maxZipTotalUncompressedBytes,
            200ULL * 1024ULL * 1024ULL
        );
        QCOMPARE(limits.maxZipCompressionRatio, 100ULL);
        QCOMPARE(limits.maxZipNestingDepth, 3ULL);
    }

    void limitOverridesMustStayPositive()
    {
        ImportLimitOverrides valid;
        valid.maxInputBytes = 1234ULL;
        QCOMPARE(resolveImportLimits(valid).maxInputBytes, 1234ULL);

        ImportLimitOverrides invalid;
        invalid.maxInputBytes = 0ULL;
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(resolveImportLimits(invalid)),
            std::invalid_argument
        );
    }

    void normalizesSafeLogicalPaths()
    {
        QCOMPARE(
            sanitizeRelativeImportPath(
                QStringLiteral("folder\\./cards//front.png")
            ).value_or(QString()),
            QStringLiteral("folder/cards/front.png")
        );
        QVERIFY(
            !sanitizeRelativeImportPath(QStringLiteral("././"))
                .has_value()
        );
        QVERIFY(
            !sanitizeRelativeImportPath(std::nullopt)
                .has_value()
        );
    }

    void rejectsAbsoluteDriveParentAndControlPaths()
    {
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                sanitizeRelativeImportPath(
                    QStringLiteral("/tmp/card.png")
                )
            ),
            std::invalid_argument
        );
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                sanitizeRelativeImportPath(
                    QStringLiteral("C:\\cards\\front.png")
                )
            ),
            std::invalid_argument
        );
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                sanitizeRelativeImportPath(
                    QStringLiteral("cards/../secret.png")
                )
            ),
            std::invalid_argument
        );

        QString control = QStringLiteral("cards/");
        control.append(QChar(0x001f));
        control.append(QStringLiteral("front.png"));
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                sanitizeRelativeImportPath(control)
            ),
            std::invalid_argument
        );
    }

    void rejectsPathsLongerThan1024Characters()
    {
        const QString tooLong(1025, QLatin1Char('a'));
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                sanitizeRelativeImportPath(tooLong)
            ),
            std::length_error
        );
    }
};

QTEST_APPLESS_MAIN(ImportBoundaryTest)

#include "ImportBoundaryTest.moc"
