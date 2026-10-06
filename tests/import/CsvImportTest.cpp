#include <QtTest>

#include "import/CsvImport.h"
#include "import/ImportFailure.h"

using namespace tcgprint::imports;

namespace {

ImportSource source(
    const QString& filename,
    const QString& text
)
{
    const QByteArray encoded = text.toUtf8();
    return ImportSource{
        .id = filename,
        .kind = ImportSourceKind::File,
        .filename = filename,
        .order = 0,
        .sizeBytes =
            static_cast<std::uint64_t>(encoded.size()),
        .originalBytes = encoded,
    };
}

} // namespace

class CsvImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void respectsQuotedFieldsAliasesAndUnknownColumns()
    {
        const ImporterOutput result = parseCsvImport(
            source(
                QStringLiteral("cards.csv"),
                QStringLiteral(
                    "Card Name,Count,Set Code,Collector Number,Language,Note\n"
                    "\"Foo, the \"\"Bar\"\"\",2,CMM,396,en,keep me\n"
                )
            )
        );

        QCOMPARE(result.entries.size(), std::size_t{1});
        const ImportedEntry& entry = result.entries.front();
        QCOMPARE(entry.quantity, 2ULL);
        QCOMPARE(
            entry.cardHint->name.value_or(QString()),
            QStringLiteral("Foo, the \"Bar\"")
        );
        QCOMPARE(
            entry.cardHint->setCode.value_or(QString()),
            QStringLiteral("CMM")
        );
        QCOMPARE(
            entry.cardHint->collectorNumber.value_or(QString()),
            QStringLiteral("396")
        );
        QCOMPARE(
            entry.cardHint->language.value_or(QString()),
            QStringLiteral("en")
        );

        const QJsonObject rawRecord =
            entry.metadata
                .value(QStringLiteral("rawRecord"))
                .toObject();
        QCOMPARE(
            rawRecord.value(QStringLiteral("Note")).toString(),
            QStringLiteral("keep me")
        );

        QCOMPARE(result.mappings.size(), std::size_t{1});
        QCOMPARE(
            result.mappings.front().unknownFields,
            QStringList{QStringLiteral("Note")}
        );
    }

    void supportsExplicitOverridesAndTsv()
    {
        CsvImportMapping mapping;
        mapping.name = QStringLiteral("Display");
        mapping.quantity = QStringLiteral("Copies");
        mapping.scryfallId = QStringLiteral("Scryfall ID");

        const ImporterOutput result = parseCsvImport(
            source(
                QStringLiteral("cards.tsv"),
                QStringLiteral(
                    "Display\tCopies\tScryfall ID\n"
                    "Sol Ring\t3\tabc-123\n"
                )
            ),
            mapping,
            QLatin1Char('\t')
        );

        QCOMPARE(result.entries.size(), std::size_t{1});
        QCOMPARE(result.entries.front().quantity, 3ULL);
        QCOMPARE(
            result.entries.front()
                .cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(
            result.entries.front()
                .cardHint->scryfallId.value_or(QString()),
            QStringLiteral("abc-123")
        );
        QCOMPARE(
            result.mappings.front().format,
            QStringLiteral("tsv")
        );
    }

    void reportsMalformedQuotingAndRowsWithoutName()
    {
        try {
            static_cast<void>(
                parseCsvImport(
                    source(
                        QStringLiteral("bad.csv"),
                        QStringLiteral(
                            "name,quantity\n\"unfinished,1"
                        )
                    )
                )
            );
            QFAIL("Expected malformed CSV failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INVALID_CSV")
            );
        }

        const ImporterOutput result = parseCsvImport(
            source(
                QStringLiteral("blank.csv"),
                QStringLiteral("name,quantity\n,2\n")
            )
        );
        QVERIFY(result.entries.empty());
        QCOMPARE(result.warnings.size(), std::size_t{1});
        QCOMPARE(
            result.warnings.front().code,
            QStringLiteral("MISSING_NAME")
        );
        QCOMPARE(result.warnings.front().line.value_or(0), 2);
    }

    void enforcesByteRowsAndMappingBounds()
    {
        ImportLimitOverrides byteLimit;
        byteLimit.maxCsvBytes = 4;
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                parseCsvImport(
                    source(
                        QStringLiteral("cards.csv"),
                        QStringLiteral("name\nSol Ring\n")
                    ),
                    std::nullopt,
                    std::nullopt,
                    byteLimit
                )
            ),
            ImportFailureError
        );

        ImportLimitOverrides rowLimit;
        rowLimit.maxCsvRows = 1;
        try {
            static_cast<void>(
                parseCsvImport(
                    source(
                        QStringLiteral("cards.csv"),
                        QStringLiteral(
                            "name\nSol Ring\nIsland\n"
                        )
                    ),
                    std::nullopt,
                    std::nullopt,
                    rowLimit
                )
            );
            QFAIL("Expected row limit failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INPUT_TOO_LARGE")
            );
        }

        CsvImportMapping invalidMapping;
        invalidMapping.name = 9;
        try {
            static_cast<void>(
                parseCsvImport(
                    source(
                        QStringLiteral("cards.csv"),
                        QStringLiteral("name\nSol Ring\n")
                    ),
                    invalidMapping
                )
            );
            QFAIL("Expected mapping failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("MAPPING_INVALID")
            );
        }
    }
};

QTEST_APPLESS_MAIN(CsvImportTest)

#include "CsvImportTest.moc"
