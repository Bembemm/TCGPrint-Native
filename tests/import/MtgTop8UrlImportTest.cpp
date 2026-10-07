#include <QtTest>

#include "import/ImportFailure.h"
#include "import/MtgTop8UrlImport.h"

using namespace tcgprint::imports;

namespace {

UrlFetchOptions mtgOptions(
    std::function<UrlHttpResponse(const QUrl&)> handler,
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
        [handler = std::move(handler), requests](
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
            return handler(url);
        };
    return options;
}

} // namespace

class MtgTop8UrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void followsExplicitExportAndParsesMwsRows()
    {
        const QString eventUrl =
            QStringLiteral(
                "https://www.mtgtop8.com/event?d=298009"
            );
        const QByteArray eventHtml =
            QByteArrayLiteral(
                "<html><a class=\"download\" "
                "href=\"/dec?d=298009&amp;f=Limited_WB_by_captainobv\">"
                "Download</a></html>"
            );

        QStringList requests;
        const auto result =
            importMtgTop8Url(
                eventUrl,
                QStringLiteral("source:mtgtop8"),
                3,
                mtgOptions(
                    [eventUrl, eventHtml](
                        const QUrl& url
                    ) {
                        if (
                            url.toString()
                            == eventUrl
                        ) {
                            return UrlHttpResponse{
                                .status = 200,
                                .headers = {
                                    {
                                        QStringLiteral(
                                            "content-type"
                                        ),
                                        QStringLiteral(
                                            "text/html; charset=utf-8"
                                        )
                                    },
                                },
                                .body = eventHtml,
                            };
                        }
                        return UrlHttpResponse{
                            .status = 200,
                            .headers = {
                                {
                                    QStringLiteral(
                                        "content-type"
                                    ),
                                    QStringLiteral(
                                        "text/plain; charset=ISO-8859-1"
                                    )
                                },
                                {
                                    QStringLiteral(
                                        "content-disposition"
                                    ),
                                    QStringLiteral(
                                        "attachment; filename=\"Limited_WB_by_captainobv.mwDeck\""
                                    )
                                },
                            },
                            .body = QByteArrayLiteral(
                                "// Deck file created with mtgtop8.com\n"
                                "6 [AKH] Swamp\n"
                                "1 [AKH] Cursed Minotaur\n"
                            ),
                        };
                    },
                    &requests
                )
            );

        QCOMPARE(
            requests,
            QStringList{
                eventUrl,
                QStringLiteral(
                    "https://www.mtgtop8.com/dec?d=298009&f=Limited_WB_by_captainobv"
                ),
            }
        );
        QCOMPARE(result.source.order, 3);
        QCOMPARE(
            result.source.adapterId.value_or(QString()),
            QStringLiteral("mtgtop8")
        );
        QCOMPARE(
            result.source.filename.value_or(QString()),
            QStringLiteral(
                "Limited_WB_by_captainobv.mwDeck"
            )
        );
        QCOMPARE(
            result.output.entries.size(),
            std::size_t{2}
        );
        QCOMPARE(
            result.output.entries[0].quantity,
            6ULL
        );
        QCOMPARE(
            result.output.entries[0]
                .cardHint->name.value_or(QString()),
            QStringLiteral("Swamp")
        );
        QCOMPARE(
            result.output.entries[0]
                .cardHint->setCode.value_or(QString()),
            QStringLiteral("AKH")
        );
        QCOMPARE(
            result.output.entries[1]
                .cardHint->name.value_or(QString()),
            QStringLiteral("Cursed Minotaur")
        );
    }

    void acceptsDirectExportAndLegacyCharset()
    {
        const QString exportUrl =
            QStringLiteral(
                "https://www.mtgtop8.com/dec?d=298009&f=Limited_WB_by_captainobv"
            );
        QByteArray body =
            QByteArrayLiteral(
                "// Deck file created with mtgtop8.com\n"
                "1 [AKH] "
            );
        body.append(char(0xc9));
        body += QByteArrayLiteral("lan\n");

        const auto result =
            importMtgTop8Url(
                exportUrl,
                QStringLiteral("source:latin1"),
                0,
                mtgOptions(
                    [body](const QUrl&) {
                        return UrlHttpResponse{
                            .status = 200,
                            .headers = {
                                {
                                    QStringLiteral(
                                        "content-type"
                                    ),
                                    QStringLiteral(
                                        "text/plain; charset=ISO-8859-1"
                                    )
                                },
                            },
                            .body = body,
                        };
                    }
                )
            );

        QCOMPARE(
            result.output.entries.size(),
            std::size_t{1}
        );
        QCOMPARE(
            result.output.entries[0]
                .cardHint->name.value_or(QString()),
            QString::fromUtf8("Élan")
        );
    }

    void rejectsUnsupportedPathsWithoutFetch()
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
                importMtgTop8Url(
                    QStringLiteral(
                        "https://www.mtgtop8.com/forum"
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

    void rejectsMissingOrAmbiguousExportLinks()
    {
        const QString eventUrl =
            QStringLiteral(
                "https://www.mtgtop8.com/event?d=298009"
            );

        for (const QByteArray& html : {
            QByteArrayLiteral(
                "<html>export link changed</html>"
            ),
            QByteArrayLiteral(
                "<a href=\"/dec?d=298009&f=ONE\">one</a>"
                "<a href=\"/dec?d=298009&f=TWO\">two</a>"
            ),
        }) {
            try {
                static_cast<void>(
                    importMtgTop8Url(
                        eventUrl,
                        QStringLiteral("source:page"),
                        0,
                        mtgOptions(
                            [eventUrl, html](
                                const QUrl& url
                            ) {
                                if (
                                    url.toString()
                                    == eventUrl
                                ) {
                                    return UrlHttpResponse{
                                        .status = 200,
                                        .headers = {
                                            {
                                                QStringLiteral(
                                                    "content-type"
                                                ),
                                                QStringLiteral(
                                                    "text/html"
                                                )
                                            },
                                        },
                                        .body = html,
                                    };
                                }
                                return UrlHttpResponse{
                                    .status = 200,
                                };
                            }
                        )
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

    void rejectsWrongExportPayload()
    {
        const QString exportUrl =
            QStringLiteral(
                "https://www.mtgtop8.com/dec?d=298009&f=Limited_WB_by_captainobv"
            );

        for (const UrlHttpResponse& response : {
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral("content-type"),
                        QStringLiteral("text/html")
                    },
                },
                .body = QByteArrayLiteral(
                    "<html>no export</html>"
                ),
            },
            UrlHttpResponse{
                .status = 200,
                .headers = {
                    {
                        QStringLiteral("content-type"),
                        QStringLiteral("text/plain")
                    },
                },
                .body = QByteArrayLiteral(
                    "not a workstation deck"
                ),
            },
        }) {
            try {
                static_cast<void>(
                    importMtgTop8Url(
                        exportUrl,
                        QStringLiteral("source:payload"),
                        0,
                        mtgOptions(
                            [response](const QUrl&) {
                                return response;
                            }
                        )
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

    void rejectsUnexpectedEventRedirect()
    {
        try {
            static_cast<void>(
                importMtgTop8Url(
                    QStringLiteral(
                        "https://www.mtgtop8.com/event?d=298009"
                    ),
                    QStringLiteral("source:redirect"),
                    0,
                    mtgOptions(
                        [](const QUrl&) {
                            return UrlHttpResponse{
                                .status = 200,
                                .headers = {
                                    {
                                        QStringLiteral(
                                            "content-type"
                                        ),
                                        QStringLiteral(
                                            "text/html"
                                        )
                                    },
                                },
                                .body = QByteArrayLiteral(
                                    "<html></html>"
                                ),
                            };
                        }
                    )
                )
            );
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral(
                    "URL_ADAPTER_PAYLOAD"
                )
            );
            return;
        }
        QFAIL("Expected URL_ADAPTER_PAYLOAD.");
    }
};

QTEST_APPLESS_MAIN(MtgTop8UrlImportTest)

#include "MtgTop8UrlImportTest.moc"
