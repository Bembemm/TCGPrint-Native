#include <QtTest>

#include "import/ImportFailure.h"
#include "import/MtgWtfUrlImport.h"

using namespace tcgprint::imports;

namespace {

UrlFetchOptions mtgWtfOptions(
    UrlHttpResponse response,
    QStringList* requests = nullptr
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
        [response = std::move(response), requests](
            const QUrl& url,
            const QString&,
            std::uint64_t,
            std::uint64_t
        ) {
            if (requests != nullptr) {
                requests->push_back(
                    url.toString()
                );
            }
            return response;
        };
    return options;
}

} // namespace

class MtgWtfUrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesExplicitDownloadEndpointAndParsesDeck()
    {
        QStringList requests;
        const auto result =
            importMtgWtfUrl(
                QStringLiteral(
                    "https://mtg.wtf/deck/m19/red-white-deck"
                ),
                QStringLiteral("source:wtf"),
                2,
                mtgWtfOptions(
                    UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral(
                                    "content-type"
                                ),
                                QStringLiteral(
                                    "text/plain; charset=utf-8"
                                )
                            },
                        },
                        .body = QByteArrayLiteral(
                            "// NAME: Red-White Deck\n"
                            "// URL: https://mtg.wtf/deck/m19/red-white-deck\n"
                            "// DATE: 2019-01-01\n"
                            "4 Lightning Strike\n"
                        ),
                    },
                    &requests
                )
            );

        QCOMPARE(
            requests.size(),
            qsizetype{1}
        );
        QCOMPARE(
            requests.front(),
            QStringLiteral(
                "https://mtg.wtf/deck/m19/red-white-deck/download"
            )
        );
        QCOMPARE(result.source.order, 2);
        QCOMPARE(
            result.source.adapterId.value_or(QString()),
            QStringLiteral("mtg-wtf")
        );
        QCOMPARE(
            result.source.sourceUrl.value_or(QString()),
            QStringLiteral(
                "https://mtg.wtf/deck/m19/red-white-deck"
            )
        );
        QCOMPARE(
            result.source.filename.value_or(QString()),
            QStringLiteral(
                "m19-red-white-deck.txt"
            )
        );
        QVERIFY(result.source.originalText.has_value());
        QVERIFY(
            !result.source.originalText->contains(
                QStringLiteral("// NAME:")
            )
        );

        QCOMPARE(
            result.output.entries.size(),
            std::size_t{1}
        );
        QCOMPARE(
            result.output.entries[0].quantity,
            4ULL
        );
        QCOMPARE(
            result.output.entries[0]
                .cardHint->name.value_or(QString()),
            QStringLiteral("Lightning Strike")
        );
    }

    void acceptsExplicitDownloadUrl()
    {
        QStringList requests;
        const QString url =
            QStringLiteral(
                "https://mtg.wtf/deck/m19/red-white-deck/download"
            );

        const auto result =
            importMtgWtfUrl(
                url,
                QStringLiteral("source:download"),
                0,
                mtgWtfOptions(
                    UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral(
                                    "content-type"
                                ),
                                QStringLiteral(
                                    "text/plain"
                                )
                            },
                        },
                        .body = QByteArrayLiteral(
                            "4 Lightning Strike\n"
                        ),
                    },
                    &requests
                )
            );

        QCOMPARE(requests.size(), qsizetype{1});
        QCOMPARE(requests.front(), url);
        QCOMPARE(
            result.output.entries[0]
                .cardHint->name.value_or(QString()),
            QStringLiteral("Lightning Strike")
        );
    }

    void usesSafeContentDispositionFilename()
    {
        const auto result =
            importMtgWtfUrl(
                QStringLiteral(
                    "https://mtg.wtf/deck/m19/red-white-deck"
                ),
                QStringLiteral("source:filename"),
                0,
                mtgWtfOptions(
                    UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral(
                                    "content-type"
                                ),
                                QStringLiteral(
                                    "text/plain"
                                )
                            },
                            {
                                QStringLiteral(
                                    "content-disposition"
                                ),
                                QStringLiteral(
                                    "attachment; filename=\"../safe-deck.txt\""
                                )
                            },
                        },
                        .body = QByteArrayLiteral(
                            "1 Sol Ring\n"
                        ),
                    }
                )
            );

        QCOMPARE(
            result.source.filename.value_or(QString()),
            QStringLiteral("safe-deck.txt")
        );
    }

    void doesNotFetchUnsupportedPaths()
    {
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
                importMtgWtfUrl(
                    QStringLiteral(
                        "https://mtg.wtf/card/m19/123"
                    ),
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

    void reportsHttpAndMalformedPayloadFailures()
    {
        try {
            static_cast<void>(
                importMtgWtfUrl(
                    QStringLiteral(
                        "https://mtg.wtf/deck/m19/red-white-deck"
                    ),
                    QStringLiteral("source:http"),
                    0,
                    mtgWtfOptions(
                        UrlHttpResponse{
                            .status = 503,
                            .body = QByteArrayLiteral(
                                "blocked"
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
        }

        for (const UrlHttpResponse& response : {
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral(
                            "content-type"
                        ),
                        QStringLiteral("text/html")
                    },
                },
                .body = QByteArrayLiteral(
                    "<html>not a deck</html>"
                ),
            },
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral(
                            "content-type"
                        ),
                        QStringLiteral("text/plain")
                    },
                },
                .body = QByteArrayLiteral(
                    "<html>not a deck</html>"
                ),
            },
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral(
                            "content-type"
                        ),
                        QStringLiteral("text/plain")
                    },
                },
                .body = QByteArrayLiteral(
                    "// comments only\n"
                ),
            },
        }) {
            try {
                static_cast<void>(
                    importMtgWtfUrl(
                        QStringLiteral(
                            "https://mtg.wtf/deck/m19/red-white-deck"
                        ),
                        QStringLiteral(
                            "source:payload"
                        ),
                        0,
                        mtgWtfOptions(response)
                    )
                );
                QFAIL(
                    "Expected URL_ADAPTER_PAYLOAD."
                );
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

QTEST_APPLESS_MAIN(MtgWtfUrlImportTest)

#include "MtgWtfUrlImportTest.moc"
