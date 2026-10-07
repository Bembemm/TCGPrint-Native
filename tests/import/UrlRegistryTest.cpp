#include <QtTest>

#include "import/ImportFailure.h"
#include "import/UrlRegistry.h"

using namespace tcgprint::imports;

class UrlRegistryTest final : public QObject
{
    Q_OBJECT

private slots:
    void recognizesKnownUnsupportedSites()
    {
        const std::vector<std::pair<QString, QString>> cases{
            {
                QStringLiteral("https://www.moxfield.com/decks/abc"),
                QStringLiteral("moxfield")
            },
            {
                QStringLiteral("https://deckstats.net/decks/1/0"),
                QStringLiteral("deckstats")
            },
            {
                QStringLiteral("https://www.mtggoldfish.com/deck/844544"),
                QStringLiteral("mtggoldfish")
            },
            {
                QStringLiteral("https://tappedout.net/mtg-decks/example/"),
                QStringLiteral("tappedout")
            },
        };

        for (const auto& [value, siteId] : cases) {
            const auto result = resolveUrlAdapter(value);
            QCOMPARE(
                static_cast<int>(result.kind),
                static_cast<int>(
                    UrlResolutionKind::KnownUnsupported
                )
            );
            QCOMPARE(result.siteId, siteId);
        }
    }

    void resolvesImplementedAdapters()
    {
        const std::vector<std::pair<QString, QString>> cases{
            {
                QStringLiteral("https://scryfall.com/card/war/235"),
                QStringLiteral("scryfall")
            },
            {
                QStringLiteral("https://mtg.wtf/deck/m19/red-white-deck"),
                QStringLiteral("mtg-wtf")
            },
            {
                QStringLiteral("https://www.mtgtop8.com/event?d=298009"),
                QStringLiteral("mtgtop8")
            },
            {
                QStringLiteral("https://archidekt.com/decks/7031486"),
                QStringLiteral("archidekt")
            },
            {
                QStringLiteral("https://cubecobra.com/cube/overview/obc"),
                QStringLiteral("cubecobra")
            },
        };

        for (const auto& [value, adapterId] : cases) {
            const auto result = resolveUrlAdapter(value);
            QCOMPARE(
                static_cast<int>(result.kind),
                static_cast<int>(
                    UrlResolutionKind::Adapter
                )
            );
            QCOMPARE(result.adapterId, adapterId);
        }
    }

    void enforcesExplicitHostAndPath()
    {
        QCOMPARE(
            static_cast<int>(
                resolveUrlAdapter(
                    QStringLiteral(
                        "https://scryfall.com/profile/example"
                    )
                ).kind
            ),
            static_cast<int>(
                UrlResolutionKind::KnownUnsupported
            )
        );

        QCOMPARE(
            static_cast<int>(
                resolveUrlAdapter(
                    QStringLiteral(
                        "https://sub.example.com/deck/1"
                    )
                ).kind
            ),
            static_cast<int>(
                UrlResolutionKind::DirectFile
            )
        );

        QCOMPARE(
            static_cast<int>(
                resolveUrlAdapter(
                    QStringLiteral(
                        "https://scryfall.com.attacker.invalid/card/war/235"
                    )
                ).kind
            ),
            static_cast<int>(
                UrlResolutionKind::DirectFile
            )
        );
    }

    void unknownHostsRemainDirectFiles()
    {
        QCOMPARE(
            static_cast<int>(
                resolveUrlAdapter(
                    QStringLiteral(
                        "https://files.example.invalid/cards.csv"
                    )
                ).kind
            ),
            static_cast<int>(
                UrlResolutionKind::DirectFile
            )
        );

        const auto api = resolveUrlAdapter(
            QStringLiteral(
                "https://api.scryfall.com/cards/war/235"
            )
        );
        QCOMPARE(
            static_cast<int>(api.kind),
            static_cast<int>(
                UrlResolutionKind::KnownUnsupported
            )
        );
        QCOMPARE(
            api.siteId,
            QStringLiteral("scryfall")
        );
    }

    void rejectsMalformedUrls()
    {
        for (const QString& value : {
            QStringLiteral("https://"),
            QStringLiteral("https:/invalid.example/deck"),
            QStringLiteral("https://[::1"),
            QStringLiteral("not a URL"),
        }) {
            try {
                static_cast<void>(
                    resolveUrlAdapter(value)
                );
                QFAIL("Expected URL_INVALID.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_INVALID")
                );
            }
        }
    }

    void rejectsUnsupportedSchemes()
    {
        for (const QString& value : {
            QStringLiteral("ftp://files.example.invalid/cards.csv"),
            QStringLiteral("file:///tmp/cards.csv"),
        }) {
            try {
                static_cast<void>(
                    resolveUrlAdapter(value)
                );
                QFAIL("Expected URL_UNSUPPORTED.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_UNSUPPORTED")
                );
            }
        }
    }

    void validatesAdapterSpecificPaths()
    {
        QCOMPARE(
            resolveUrlAdapter(
                QStringLiteral(
                    "https://scryfall.com/card/war/235/teferi-time-raveler"
                )
            ).adapterId,
            QStringLiteral("scryfall")
        );
        QCOMPARE(
            resolveUrlAdapter(
                QStringLiteral(
                    "https://www.mtgtop8.com/dec?d=298009&f=MO"
                )
            ).adapterId,
            QStringLiteral("mtgtop8")
        );
        QCOMPARE(
            resolveUrlAdapter(
                QStringLiteral(
                    "https://mtg.wtf/deck/m19/red-white-deck/download"
                )
            ).adapterId,
            QStringLiteral("mtg-wtf")
        );

        const auto wrongProtocol = resolveUrlAdapter(
            QStringLiteral(
                "http://archidekt.com/decks/7031486"
            )
        );
        QCOMPARE(
            static_cast<int>(wrongProtocol.kind),
            static_cast<int>(
                UrlResolutionKind::KnownUnsupported
            )
        );
    }
};

QTEST_APPLESS_MAIN(UrlRegistryTest)

#include "UrlRegistryTest.moc"
