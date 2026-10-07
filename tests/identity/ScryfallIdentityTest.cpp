#include <QtTest>

#include "identity/ScryfallIdentity.h"

using namespace tcgprint;

class ScryfallIdentityTest final : public QObject
{
    Q_OBJECT

private slots:
    void mapsStableOracleIdentityAndMetadata()
    {
        const QJsonObject payload{
            {
                QStringLiteral("id"),
                QStringLiteral(
                    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
                )
            },
            {
                QStringLiteral("oracle_id"),
                QStringLiteral(
                    "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
                )
            },
            {
                QStringLiteral("name"),
                QStringLiteral(
                    "Delver of Secrets // Insectile Aberration"
                )
            },
            {
                QStringLiteral("layout"),
                QStringLiteral("transform")
            },
            {
                QStringLiteral("set"),
                QStringLiteral("isd")
            },
            {
                QStringLiteral("collector_number"),
                QStringLiteral("51")
            },
            {
                QStringLiteral("lang"),
                QStringLiteral("en")
            },
            {
                QStringLiteral("digital"),
                false
            },
            {
                QStringLiteral("promo"),
                false
            },
            {
                QStringLiteral("full_art"),
                false
            },
            {
                QStringLiteral("image_status"),
                QStringLiteral("highres_scan")
            },
            {
                QStringLiteral("card_faces"),
                QJsonArray{
                    QJsonObject{
                        {
                            QStringLiteral("name"),
                            QStringLiteral(
                                "Delver of Secrets"
                            )
                        },
                    },
                    QJsonObject{
                        {
                            QStringLiteral("name"),
                            QStringLiteral(
                                "Insectile Aberration"
                            )
                        },
                    },
                }
            },
            {
                QStringLiteral("all_parts"),
                QJsonArray{
                    QJsonObject{
                        {
                            QStringLiteral("id"),
                            QStringLiteral("related-id")
                        },
                        {
                            QStringLiteral("component"),
                            QStringLiteral("combo_piece")
                        },
                        {
                            QStringLiteral("name"),
                            QStringLiteral("Related Card")
                        },
                        {
                            QStringLiteral("type_line"),
                            QStringLiteral("Token Creature")
                        },
                    },
                }
            },
        };

        const auto parsed =
            identity::parseScryfallIdentityCard(payload);
        const cards::CardIdentity mapped =
            identity::toCardIdentity(
                parsed,
                cards::IdentityResolutionMethod::ScryfallId,
                1.0
            );

        QCOMPARE(
            QString::fromStdString(mapped.provider),
            QStringLiteral("scryfall")
        );
        QCOMPARE(
            QString::fromStdString(mapped.id),
            QStringLiteral(
                "scryfall:oracle:bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
            )
        );
        QCOMPARE(
            QString::fromStdString(
                mapped.scryfallId.value()
            ),
            QStringLiteral(
                "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
            )
        );
        QCOMPARE(
            QString::fromStdString(
                mapped.setCode.value()
            ),
            QStringLiteral("isd")
        );
        QCOMPARE(
            mapped.resolutionMethod,
            cards::IdentityResolutionMethod::ScryfallId
        );
        QCOMPARE(mapped.confidence, 1.0);
        QVERIFY(mapped.metadata.has_value());
        QCOMPARE(
            QString::fromStdString(
                std::get<std::string>(
                    mapped.metadata->scalars.at("layout")
                )
            ),
            QStringLiteral("transform")
        );
        QCOMPARE(
            mapped.metadata->faces.size(),
            std::size_t{2}
        );
        QCOMPARE(
            QString::fromStdString(
                mapped.metadata->faces[1].name
            ),
            QStringLiteral("Insectile Aberration")
        );
        QCOMPARE(
            mapped.metadata->relatedCards.size(),
            std::size_t{1}
        );
        QCOMPARE(
            QString::fromStdString(
                mapped.metadata
                    ->relatedCards[0]
                    .component
            ),
            QStringLiteral("combo_piece")
        );
    }

    void fallsBackToPrintingIdentityWithoutOracleId()
    {
        const QJsonObject payload{
            {
                QStringLiteral("id"),
                QStringLiteral("printing-id")
            },
            {
                QStringLiteral("name"),
                QStringLiteral("Custom Printing")
            },
            {
                QStringLiteral("layout"),
                QStringLiteral("normal")
            },
        };

        const auto mapped = identity::toCardIdentity(
            identity::parseScryfallIdentityCard(payload),
            cards::IdentityResolutionMethod::Name,
            0.9
        );

        QCOMPARE(
            QString::fromStdString(mapped.id),
            QStringLiteral(
                "scryfall:card:printing-id"
            )
        );
        QVERIFY(!mapped.oracleId.has_value());
        QCOMPARE(
            mapped.resolutionMethod,
            cards::IdentityResolutionMethod::Name
        );
        QCOMPARE(mapped.confidence, 0.9);
    }

    void rejectsMalformedProviderShapes()
    {
        const QList<QJsonValue> invalid{
            QJsonArray{},
            QJsonObject{
                {
                    QStringLiteral("name"),
                    QStringLiteral("Missing ID")
                },
                {
                    QStringLiteral("layout"),
                    QStringLiteral("normal")
                },
            },
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("x")
                },
                {
                    QStringLiteral("name"),
                    QStringLiteral("Bad Faces")
                },
                {
                    QStringLiteral("layout"),
                    QStringLiteral("normal")
                },
                {
                    QStringLiteral("card_faces"),
                    QStringLiteral("not-an-array")
                },
            },
            QJsonObject{
                {
                    QStringLiteral("id"),
                    QStringLiteral("x")
                },
                {
                    QStringLiteral("name"),
                    QStringLiteral("Bad Bool")
                },
                {
                    QStringLiteral("layout"),
                    QStringLiteral("normal")
                },
                {
                    QStringLiteral("digital"),
                    QStringLiteral("false")
                },
            },
        };

        for (const QJsonValue& value : invalid) {
            QVERIFY_EXCEPTION_THROWN(
                identity::parseScryfallIdentityCard(value),
                identity::ScryfallIdentityPayloadError
            );
        }
    }

    void validatesConfidenceRange()
    {
        const identity::ScryfallIdentityCard card{
            .id = QStringLiteral("printing"),
            .name = QStringLiteral("Name"),
            .layout = QStringLiteral("normal"),
        };

        QVERIFY_EXCEPTION_THROWN(
            identity::toCardIdentity(
                card,
                cards::IdentityResolutionMethod::Name,
                1.1
            ),
            std::invalid_argument
        );
    }
};

QTEST_APPLESS_MAIN(ScryfallIdentityTest)

#include "ScryfallIdentityTest.moc"
