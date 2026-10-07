#include <QtTest>

#include "import/ImportFailure.h"
#include "import/ScryfallUrlImport.h"

using namespace tcgprint::imports;

class ScryfallUrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsExplicitCardUrlWithoutNetwork()
    {
        const auto result =
            importScryfallCardUrl(
                QStringLiteral(
                    "https://scryfall.com/card/war/235/the-war-in-the-spark"
                ),
                QStringLiteral("source:scryfall"),
                9
            );

        QCOMPARE(result.source.order, 9);
        QCOMPARE(
            result.source.adapterId.value_or(QString()),
            QStringLiteral("scryfall")
        );
        QCOMPARE(
            result.source.sourceUrl.value_or(QString()),
            QStringLiteral(
                "https://scryfall.com/card/war/235/the-war-in-the-spark"
            )
        );
        QCOMPARE(result.source.sizeBytes, 0ULL);

        QCOMPARE(
            result.output.entries.size(),
            std::size_t{1}
        );
        const ImportedEntry& entry =
            result.output.entries.front();

        QCOMPARE(
            static_cast<int>(entry.kind),
            static_cast<int>(
                ImportedEntryKind::DeckCard
            )
        );
        QCOMPARE(entry.quantity, 1ULL);
        QCOMPARE(entry.order, 0);
        QVERIFY(entry.cardHint.has_value());
        QCOMPARE(
            entry.cardHint->setCode.value_or(QString()),
            QStringLiteral("war")
        );
        QCOMPARE(
            entry.cardHint
                ->collectorNumber.value_or(QString()),
            QStringLiteral("235")
        );
        QCOMPARE(
            entry.nameSuggestion.value_or(QString()),
            QStringLiteral("The War in the Spark")
        );
        QCOMPARE(
            entry.metadata
                .value(QStringLiteral("source"))
                .toString(),
            QStringLiteral("card-url")
        );
    }

    void acceptsWwwAndMissingSlug()
    {
        const auto result =
            importScryfallCardUrl(
                QStringLiteral(
                    "https://www.scryfall.com/card/MH3/123"
                ),
                QStringLiteral("source:www")
            );

        const ImportedEntry& entry =
            result.output.entries.front();
        QCOMPARE(
            entry.cardHint->setCode.value_or(QString()),
            QStringLiteral("mh3")
        );
        QCOMPARE(
            entry.cardHint
                ->collectorNumber.value_or(QString()),
            QStringLiteral("123")
        );
        QVERIFY(!entry.nameSuggestion.has_value());
    }

    void preservesCollectorStars()
    {
        const auto result =
            importScryfallCardUrl(
                QStringLiteral(
                    "https://scryfall.com/card/unh/123*"
                ),
                QStringLiteral("source:star")
            );

        QCOMPARE(
            result.output.entries.front()
                .cardHint
                ->collectorNumber.value_or(QString()),
            QStringLiteral("123*")
        );
    }

    void rejectsUnsupportedScryfallPaths()
    {
        for (const QString& url : {
            QStringLiteral(
                "https://scryfall.com/search?q=sol+ring"
            ),
            QStringLiteral(
                "https://scryfall.com/card/war/not-a-collector/name"
            ),
            QStringLiteral(
                "https://scryfall.com/card/war/235/name/extra"
            ),
            QStringLiteral(
                "http://scryfall.com/card/war/235"
            ),
        }) {
            try {
                static_cast<void>(
                    importScryfallCardUrl(
                        url,
                        QStringLiteral("source:bad")
                    )
                );
                QFAIL("Expected URL_UNSUPPORTED.");
            } catch (
                const ImportFailureError& error
            ) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_UNSUPPORTED")
                );
            }
        }
    }

    void rejectsOtherAdapters()
    {
        try {
            static_cast<void>(
                importScryfallCardUrl(
                    QStringLiteral(
                        "https://archidekt.com/decks/7031486"
                    ),
                    QStringLiteral("source:other")
                )
            );
            QFAIL("Expected URL_UNSUPPORTED.");
        } catch (
            const ImportFailureError& error
        ) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_UNSUPPORTED")
            );
        }
    }
};

QTEST_APPLESS_MAIN(ScryfallUrlImportTest)

#include "ScryfallUrlImportTest.moc"
