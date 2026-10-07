#include <QtTest>

#include "import/ArchidektUrlImport.h"
#include "import/ImportFailure.h"

using namespace tcgprint::imports;

namespace {

QByteArray archidektFixture()
{
    return QByteArrayLiteral(
        "{"
        "\"id\":7031486,"
        "\"name\":\"Synthetic Archidekt deck\","
        "\"cards\":["
        "{"
        "\"quantity\":2,"
        "\"categories\":[\"Creatures\",\"Mainboard\"],"
        "\"card\":{"
        "\"collectorNumber\":\"182\","
        "\"edition\":{\"editioncode\":\"bro\"},"
        "\"oracleCard\":{\"name\":\"Gaea's Gift\"}"
        "}"
        "},"
        "{"
        "\"quantity\":1,"
        "\"categories\":[\"Sideboard\"],"
        "\"card\":{"
        "\"collectorNumber\":\"17\","
        "\"edition\":{\"editioncode\":\"m21\"},"
        "\"oracleCard\":{\"name\":\"Example Card\"}"
        "}"
        "}"
        "]"
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

class ArchidektUrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void fetchesApiAndNormalizesCards()
    {
        QString requestedUrl;
        const auto result =
            importArchidektDeckUrl(
                QStringLiteral(
                    "https://archidekt.com/decks/7031486"
                ),
                QStringLiteral("source:archidekt"),
                5,
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
                        .body = archidektFixture(),
                    },
                    &requestedUrl
                )
            );

        QCOMPARE(
            requestedUrl,
            QStringLiteral(
                "https://archidekt.com/api/decks/7031486/"
            )
        );
        QCOMPARE(result.source.order, 5);
        QCOMPARE(
            result.source.adapterId.value_or(QString()),
            QStringLiteral("archidekt")
        );
        QCOMPARE(
            result.source.mediaType.value_or(QString()),
            QStringLiteral("application/json")
        );
        QCOMPARE(
            result.source.sourceUrl.value_or(QString()),
            QStringLiteral(
                "https://archidekt.com/decks/7031486"
            )
        );
        QVERIFY(result.source.originalBytes.has_value());
        QVERIFY(result.source.sha256.has_value());

        QCOMPARE(
            result.output.entries.size(),
            std::size_t{2}
        );

        const ImportedEntry& first =
            result.output.entries[0];
        QCOMPARE(first.quantity, 2ULL);
        QVERIFY(first.cardHint.has_value());
        QCOMPARE(
            first.cardHint->name.value_or(QString()),
            QStringLiteral("Gaea's Gift")
        );
        QCOMPARE(
            first.cardHint->setCode.value_or(QString()),
            QStringLiteral("bro")
        );
        QCOMPARE(
            first.cardHint
                ->collectorNumber.value_or(QString()),
            QStringLiteral("182")
        );
        QCOMPARE(
            first.cardHint->section.value_or(QString()),
            QStringLiteral("Creatures, Mainboard")
        );

        const ImportedEntry& second =
            result.output.entries[1];
        QCOMPARE(second.quantity, 1ULL);
        QCOMPARE(
            second.cardHint->name.value_or(QString()),
            QStringLiteral("Example Card")
        );
        QCOMPARE(
            second.cardHint->section.value_or(QString()),
            QStringLiteral("Sideboard")
        );
        QCOMPARE(
            result.source.metadata
                .value(QStringLiteral("deckName"))
                .toString(),
            QStringLiteral("Synthetic Archidekt deck")
        );
    }

    void rejectsMalformedPathsWithoutFetch()
    {
        for (const QString& value : {
            QStringLiteral(
                "https://archidekt.com/decks/not-a-number"
            ),
            QStringLiteral(
                "https://archidekt.com/decks/7031486/cards"
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
                    importArchidektDeckUrl(
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

    void reportsHttpFailureWithoutBodyLeak()
    {
        try {
            static_cast<void>(
                importArchidektDeckUrl(
                    QStringLiteral(
                        "https://archidekt.com/decks/7031486"
                    ),
                    QStringLiteral("source:http"),
                    0,
                    apiOptions(
                        UrlHttpResponse{
                            .status = 403,
                            .body = QByteArrayLiteral(
                                "private error body"
                            ),
                        }
                    )
                )
            );
            QFAIL("Expected URL_HTTP_ERROR.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_HTTP_ERROR")
            );
            QVERIFY(
                !QString::fromUtf8(error.what())
                    .contains(
                        QStringLiteral(
                            "private error body"
                        )
                    )
            );
        }
    }

    void rejectsMalformedSchema()
    {
        try {
            static_cast<void>(
                importArchidektDeckUrl(
                    QStringLiteral(
                        "https://archidekt.com/decks/7031486"
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
                            .body = QByteArrayLiteral(
                                "{\"id\":7031486,\"cards\":[{\"quantity\":1}]}"
                            ),
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
                .body = archidektFixture(),
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
                    importArchidektDeckUrl(
                        QStringLiteral(
                            "https://archidekt.com/decks/7031486"
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

QTEST_APPLESS_MAIN(ArchidektUrlImportTest)

#include "ArchidektUrlImportTest.moc"
