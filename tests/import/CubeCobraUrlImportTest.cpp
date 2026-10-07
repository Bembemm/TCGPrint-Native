#include <QtTest>

#include "import/CubeCobraUrlImport.h"
#include "import/ImportFailure.h"

using namespace tcgprint::imports;

namespace {

QByteArray cubeFixture()
{
    return QByteArrayLiteral(
        "{"
        "\"id\":\"synthetic-cube\","
        "\"name\":\"Synthetic CubeCobra cube\","
        "\"cards\":{"
        "\"id\":\"synthetic-cube\","
        "\"mainboard\":[{"
        "\"details\":{"
        "\"name\":\"Sol Ring\","
        "\"set\":\"cmm\","
        "\"collector_number\":\"396\","
        "\"scryfall_id\":\"11111111-1111-1111-1111-111111111111\""
        "},"
        "\"board\":\"mainboard\""
        "}],"
        "\"maybeboard\":[{"
        "\"details\":{"
        "\"name\":\"Lightning Bolt\","
        "\"set\":\"lea\","
        "\"collector_number\":\"161\""
        "},"
        "\"board\":\"maybeboard\""
        "}],"
        "\"basics\":[{"
        "\"details\":{"
        "\"name\":\"Island\","
        "\"set\":\"m21\","
        "\"collector_number\":\"310\""
        "},"
        "\"board\":\"basics\""
        "}]"
        "}"
        "}"
    );
}

UrlFetchOptions apiOptions(
    UrlHttpResponse response,
    QString* requestedUrl = nullptr
)
{
    UrlFetchOptions options;
    options.resolveHost =
        [](const QString&) {
            return QStringList{
                QStringLiteral("93.184.216.34")
            };
        };
    options.requestExecutor =
        [response = std::move(response), requestedUrl](
            const QUrl& url,
            const QString&,
            std::uint64_t,
            std::uint64_t
        ) {
            if (requestedUrl != nullptr) {
                *requestedUrl = url.toString();
            }
            return response;
        };
    return options;
}

} // namespace

class CubeCobraUrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void fetchesApiAndPreservesBoardsAndHints()
    {
        QString requestedUrl;
        const auto result =
            importCubeCobraUrl(
                QStringLiteral(
                    "https://cubecobra.com/cube/overview/synthetic-cube"
                ),
                QStringLiteral("source:cube"),
                4,
                apiOptions(
                    UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral("content-type"),
                                QStringLiteral(
                                    "application/json; charset=utf-8"
                                )
                            },
                        },
                        .body = cubeFixture(),
                    },
                    &requestedUrl
                )
            );

        QCOMPARE(
            requestedUrl,
            QStringLiteral(
                "https://cubecobra.com/cube/api/cubeJSON/synthetic-cube"
            )
        );
        QCOMPARE(result.source.order, 4);
        QCOMPARE(
            result.source.adapterId.value_or(QString()),
            QStringLiteral("cubecobra")
        );
        QCOMPARE(
            result.source.sourceUrl.value_or(QString()),
            QStringLiteral(
                "https://cubecobra.com/cube/overview/synthetic-cube"
            )
        );
        QCOMPARE(
            result.output.entries.size(),
            std::size_t{3}
        );

        const ImportedEntry& main =
            result.output.entries[0];
        QCOMPARE(
            main.cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(
            main.cardHint->setCode.value_or(QString()),
            QStringLiteral("cmm")
        );
        QCOMPARE(
            main.cardHint
                ->collectorNumber.value_or(QString()),
            QStringLiteral("396")
        );
        QCOMPARE(
            main.cardHint->section.value_or(QString()),
            QStringLiteral("Mainboard")
        );
        QCOMPARE(
            main.cardHint->scryfallId.value_or(QString()),
            QStringLiteral(
                "11111111-1111-1111-1111-111111111111"
            )
        );

        QCOMPARE(
            result.output.entries[1]
                .cardHint->section.value_or(QString()),
            QStringLiteral("Maybeboard")
        );
        QCOMPARE(
            result.output.entries[2]
                .cardHint->section.value_or(QString()),
            QStringLiteral("Basics")
        );
        QCOMPARE(
            result.source.metadata
                .value(QStringLiteral("cubeName"))
                .toString(),
            QStringLiteral("Synthetic CubeCobra cube")
        );
    }

    void supportsQuantityAliasesAndDefault()
    {
        const QByteArray payload = QByteArrayLiteral(
            "{"
            "\"cards\":{"
            "\"mainboard\":["
            "{\"quantity\":2,\"details\":{\"name\":\"A\"}},"
            "{\"count\":3,\"details\":{\"name\":\"B\"}},"
            "{\"qty\":4,\"details\":{\"name\":\"C\"}},"
            "{\"details\":{\"name\":\"D\"}}"
            "]"
            "}"
            "}"
        );

        const auto result = importCubeCobraUrl(
            QStringLiteral(
                "https://cubecobra.com/cube/overview/ab"
            ),
            QStringLiteral("source:qty"),
            0,
            apiOptions(
                UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-type"),
                            QStringLiteral("application/json")
                        },
                    },
                    .body = payload,
                }
            )
        );

        QCOMPARE(result.output.entries[0].quantity, 2ULL);
        QCOMPARE(result.output.entries[1].quantity, 3ULL);
        QCOMPARE(result.output.entries[2].quantity, 4ULL);
        QCOMPARE(result.output.entries[3].quantity, 1ULL);
    }

    void rejectsMalformedPathsWithoutFetch()
    {
        for (const QString& value : {
            QStringLiteral(
                "https://cubecobra.com/cube/overview/a"
            ),
            QStringLiteral(
                "https://cubecobra.com/cube/list/synthetic-cube"
            ),
        }) {
            int calls = 0;
            UrlFetchOptions options;
            options.resolveHost =
                [](const QString&) {
                    return QStringList{
                        QStringLiteral("93.184.216.34")
                    };
                };
            options.requestExecutor =
                [&calls](
                    const QUrl&,
                    const QString&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    ++calls;
                    return UrlHttpResponse{
                        .status = 200,
                    };
                };

            try {
                static_cast<void>(
                    importCubeCobraUrl(
                        value,
                        QStringLiteral("source:bad"),
                        0,
                        options
                    )
                );
                QFAIL("Expected URL_UNSUPPORTED.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_UNSUPPORTED")
                );
            }
            QCOMPARE(calls, 0);
        }
    }

    void rejectsMalformedSchemaAndEmptyCube()
    {
        for (const QByteArray& payload : {
            QByteArrayLiteral(
                "{\"id\":\"x\",\"cards\":{\"mainboard\":\"not-an-array\"}}"
            ),
            QByteArrayLiteral(
                "{\"id\":\"x\",\"cards\":{\"mainboard\":[]}}"
            ),
        }) {
            try {
                static_cast<void>(
                    importCubeCobraUrl(
                        QStringLiteral(
                            "https://cubecobra.com/cube/overview/test-cube"
                        ),
                        QStringLiteral("source:schema"),
                        0,
                        apiOptions(
                            UrlHttpResponse{
                                .status = 200,
                                .headers = {
                                    {
                                        QStringLiteral(
                                            "content-type"
                                        ),
                                        QStringLiteral(
                                            "application/json"
                                        )
                                    },
                                },
                                .body = payload,
                            }
                        )
                    )
                );
                QFAIL("Expected URL_ADAPTER_PAYLOAD.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral(
                        "URL_ADAPTER_PAYLOAD"
                    )
                );
            }
        }
    }

    void rejectsWrongContentTypeAndInvalidUtf8()
    {
        for (const UrlHttpResponse& response : {
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral("content-type"),
                        QStringLiteral("text/plain")
                    },
                },
                .body = cubeFixture(),
            },
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral("content-type"),
                        QStringLiteral("application/json")
                    },
                },
                .body = QByteArray::fromHex("fffefd"),
            },
        }) {
            try {
                static_cast<void>(
                    importCubeCobraUrl(
                        QStringLiteral(
                            "https://cubecobra.com/cube/overview/test-cube"
                        ),
                        QStringLiteral("source:payload"),
                        0,
                        apiOptions(response)
                    )
                );
                QFAIL("Expected URL_ADAPTER_PAYLOAD.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral(
                        "URL_ADAPTER_PAYLOAD"
                    )
                );
            }
        }
    }
};

QTEST_APPLESS_MAIN(CubeCobraUrlImportTest)

#include "CubeCobraUrlImportTest.moc"
