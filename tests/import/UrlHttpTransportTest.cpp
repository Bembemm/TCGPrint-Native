#include <QtTest>

#include "import/ImportFailure.h"
#include "import/UrlHttpTransport.h"

#include <vector>

using namespace tcgprint::imports;

namespace {

UrlFetchOptions fakeOptions(
    UrlPinnedRequestExecutor executor
)
{
    UrlFetchOptions options;
    options.resolveHost =
        [](const QString&) {
            return QStringList{
                QStringLiteral("93.184.216.34")
            };
        };
    options.requestExecutor = std::move(executor);
    return options;
}

} // namespace

class UrlHttpTransportTest final : public QObject
{
    Q_OBJECT

private slots:
    void fetchesBoundedPayloadThroughPinnedAddress()
    {
        QString seenAddress;
        const UrlPayload result = fetchUrlPayload(
            QStringLiteral(
                "https://files.example.invalid/deck.txt"
            ),
            fakeOptions(
                [&seenAddress](
                    const QUrl&,
                    const QString& address,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenAddress = address;
                    return UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral("content-type"),
                                QStringLiteral(
                                    "text/plain; charset=utf-8"
                                )
                            },
                            {
                                QStringLiteral("content-length"),
                                QStringLiteral("10")
                            },
                        },
                        .body = QByteArrayLiteral("1 Sol Ring"),
                    };
                }
            )
        );

        QCOMPARE(
            seenAddress,
            QStringLiteral("93.184.216.34")
        );
        QCOMPARE(
            result.bytes,
            QByteArrayLiteral("1 Sol Ring")
        );
        QCOMPARE(
            result.mediaType,
            QStringLiteral("text/plain")
        );
        QCOMPARE(
            result.finalUrl.toString(),
            QStringLiteral(
                "https://files.example.invalid/deck.txt"
            )
        );
    }

    void followsValidatedRedirects()
    {
        int calls = 0;
        const UrlPayload result = fetchUrlPayload(
            QStringLiteral(
                "https://files.example.invalid/start"
            ),
            fakeOptions(
                [&calls](
                    const QUrl& url,
                    const QString&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    ++calls;
                    if (
                        url.path()
                        == QStringLiteral("/start")
                    ) {
                        return UrlHttpResponse{
                            .status = 302,
                            .headers = {
                                {
                                    QStringLiteral("location"),
                                    QStringLiteral("/deck.txt")
                                },
                            },
                        };
                    }
                    return UrlHttpResponse{
                        .status = 200,
                        .headers = {
                            {
                                QStringLiteral("content-type"),
                                QStringLiteral("text/plain")
                            },
                        },
                        .body = QByteArrayLiteral("1 Sol Ring"),
                    };
                }
            )
        );

        QCOMPARE(calls, 2);
        QCOMPARE(
            result.finalUrl.path(),
            QStringLiteral("/deck.txt")
        );
    }

    void blocksPrivateRedirectBeforeSecondRequest()
    {
        int calls = 0;
        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "https://files.example.invalid/start"
                    ),
                    fakeOptions(
                        [&calls](
                            const QUrl&,
                            const QString&,
                            std::uint64_t,
                            std::uint64_t
                        ) {
                            ++calls;
                            return UrlHttpResponse{
                                .status = 302,
                                .headers = {
                                    {
                                        QStringLiteral("location"),
                                        QStringLiteral(
                                            "http://127.0.0.1/admin"
                                        )
                                    },
                                },
                            };
                        }
                    )
                )
            );
            QFAIL("Expected URL_REDIRECT_BLOCKED.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_REDIRECT_BLOCKED")
            );
        }
        QCOMPARE(calls, 1);
    }

    void blocksPrivateDnsAnswersBeforeRequest()
    {
        int calls = 0;
        UrlFetchOptions options;
        options.resolveHost =
            [](const QString&) {
                return QStringList{
                    QStringLiteral("10.0.0.4")
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
                return UrlHttpResponse{.status = 200};
            };

        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "https://files.example.invalid/deck.txt"
                    ),
                    options
                )
            );
            QFAIL("Expected URL_HOST_BLOCKED.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_HOST_BLOCKED")
            );
        }
        QCOMPARE(calls, 0);
    }

    void reportsHttpErrorsWithoutBodyImport()
    {
        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "https://files.example.invalid/missing.csv"
                    ),
                    fakeOptions(
                        [](
                            const QUrl&,
                            const QString&,
                            std::uint64_t,
                            std::uint64_t
                        ) {
                            return UrlHttpResponse{
                                .status = 404,
                            };
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
    }

    void enforcesResponseLimitForInjectedTransport()
    {
        UrlFetchOptions options = fakeOptions(
            [](
                const QUrl&,
                const QString&,
                std::uint64_t,
                std::uint64_t
            ) {
                return UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-length"),
                            QStringLiteral("5")
                        },
                    },
                    .body = QByteArrayLiteral("12345"),
                };
            }
        );
        options.policy.maxResponseBytes = 4;

        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "https://files.example.invalid/deck.txt"
                    ),
                    options
                )
            );
            QFAIL("Expected URL_RESPONSE_LIMIT.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_RESPONSE_LIMIT")
            );
        }
    }

    void enforcesRedirectLimit()
    {
        UrlFetchOptions options = fakeOptions(
            [](
                const QUrl&,
                const QString&,
                std::uint64_t,
                std::uint64_t
            ) {
                return UrlHttpResponse{
                    .status = 302,
                    .headers = {
                        {
                            QStringLiteral("location"),
                            QStringLiteral("/again")
                        },
                    },
                };
            }
        );
        options.policy.maxRedirects = 1;

        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "https://files.example.invalid/start"
                    ),
                    options
                )
            );
            QFAIL("Expected URL_REDIRECT_BLOCKED.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_REDIRECT_BLOCKED")
            );
        }
    }

    void rejectsNonHttpInputBeforeResolving()
    {
        int resolverCalls = 0;
        UrlFetchOptions options;
        options.resolveHost =
            [&resolverCalls](const QString&) {
                ++resolverCalls;
                return QStringList{
                    QStringLiteral("93.184.216.34")
                };
            };

        try {
            static_cast<void>(
                fetchUrlPayload(
                    QStringLiteral(
                        "file:///tmp/deck.txt"
                    ),
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
        QCOMPARE(resolverCalls, 0);
    }
};

QTEST_APPLESS_MAIN(UrlHttpTransportTest)

#include "UrlHttpTransportTest.moc"
