#include <QtTest>

#include "import/ImportFailure.h"
#include "import/TextImport.h"

using namespace tcgprint::imports;

class TextImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesBareNamesAndQuantityPrefixes()
    {
        const ImporterOutput result = parseTextImport(
            QStringLiteral(
                "Sol Ring\n1 Sol Ring\n1x Sol Ring\n10 Island"
            ),
            ImportKind::SimpleDecklist
        );

        QCOMPARE(result.entries.size(), std::size_t{4});
        QCOMPARE(result.entries[0].quantity, 1ULL);
        QCOMPARE(
            result.entries[0].cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(result.entries[1].quantity, 1ULL);
        QCOMPARE(result.entries[2].quantity, 1ULL);
        QCOMPARE(result.entries[3].quantity, 10ULL);
        QCOMPARE(
            result.entries[3].cardHint->name.value_or(QString()),
            QStringLiteral("Island")
        );
    }

    void keepsPrintingDataAsHintsOnly()
    {
        const ImporterOutput result = parseTextImport(
            QStringLiteral("1 Sol Ring (CMM) 396"),
            ImportKind::SimpleDecklist
        );

        QCOMPARE(result.entries.size(), std::size_t{1});
        const ImportedEntry& entry = result.entries.front();
        QCOMPARE(
            static_cast<int>(entry.kind),
            static_cast<int>(ImportedEntryKind::DeckCard)
        );
        QCOMPARE(entry.quantity, 1ULL);
        QCOMPARE(
            entry.cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
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
            entry.metadata.value(QStringLiteral("rawLine")).toString(),
            QStringLiteral("1 Sol Ring (CMM) 396")
        );
    }

    void preservesSectionsAndWarnsForUnparsedLines()
    {
        const ImporterOutput result = parseTextImport(
            QStringLiteral(
                "Commander\n1 Sol Ring\nMainboard\n10 Island\nMaybeboard\n0x ???"
            ),
            ImportKind::SimpleDecklist
        );

        QCOMPARE(result.entries.size(), std::size_t{2});
        QCOMPARE(
            result.entries[0].section.value_or(QString()),
            QStringLiteral("Commander")
        );
        QCOMPARE(
            result.entries[1].section.value_or(QString()),
            QStringLiteral("Mainboard")
        );
        QCOMPARE(result.warnings.size(), std::size_t{1});
        QCOMPARE(
            result.warnings[0].code,
            QStringLiteral("UNPARSED_LINE")
        );
        QCOMPARE(result.warnings[0].line.value_or(0), 6);
    }

    void isolatesArenaMtgoXmageAndMwsAdapters()
    {
        const ImporterOutput arena = parseTextImport(
            QStringLiteral(
                "Deck\n1 Sol Ring (CMM) 396\nSideboard\n2 Island (M21) 265"
            ),
            ImportKind::ArenaLike
        );
        const ImporterOutput mtgo = parseTextImport(
            QStringLiteral("1 Sol Ring\nSB: 2 Island"),
            ImportKind::MtgoLike
        );
        const ImporterOutput xmage = parseTextImport(
            QStringLiteral(
                "LAYOUT MAIN\n1 Sol Ring\nLAYOUT SIDEBOARD\n2 Island"
            ),
            ImportKind::XmageLike
        );

        ImportSource xmageSource{
            .id = QStringLiteral("xmage-main"),
            .kind = ImportSourceKind::File,
            .filename = QStringLiteral("main.dck"),
            .order = 0,
            .sizeBytes = 22,
            .originalText =
                QStringLiteral("LAYOUT MAIN\n1 Sol Ring"),
        };
        const ImporterOutput xmageMain = parseTextImport(
            QStringLiteral("LAYOUT MAIN\n1 Sol Ring"),
            ImportKind::XmageLike,
            xmageSource
        );
        const ImporterOutput mws = parseTextImport(
            QStringLiteral(
                "Deck file for Magic Workstation\n1 [CMM] Sol Ring"
            ),
            ImportKind::MwdeckLike
        );

        QCOMPARE(
            arena.entries[0].section.value_or(QString()),
            QStringLiteral("Mainboard")
        );
        QCOMPARE(
            arena.entries[1].section.value_or(QString()),
            QStringLiteral("Sideboard")
        );
        QVERIFY(!mtgo.entries[0].section.has_value());
        QCOMPARE(
            mtgo.entries[1].section.value_or(QString()),
            QStringLiteral("Sideboard")
        );
        QCOMPARE(
            xmage.entries[0].section.value_or(QString()),
            QStringLiteral("Mainboard")
        );
        QCOMPARE(
            xmage.entries[1].section.value_or(QString()),
            QStringLiteral("Sideboard")
        );
        QCOMPARE(
            xmageMain.entries[0]
                .cardHint->section.value_or(QString()),
            QStringLiteral("Mainboard")
        );
        QCOMPARE(mws.entries[0].quantity, 1ULL);
        QCOMPARE(
            mws.entries[0].cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(
            mws.entries[0].cardHint->setCode.value_or(QString()),
            QStringLiteral("CMM")
        );
    }

    void rejectsExplicitAdapterMismatch()
    {
        try {
            static_cast<void>(
                parseTextImport(
                    QStringLiteral("random words"),
                    ImportKind::ArenaLike
                )
            );
            QFAIL("Expected format mismatch.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("FORMAT_MISMATCH")
            );
        }
    }
};

QTEST_APPLESS_MAIN(TextImportTest)

#include "TextImportTest.moc"
