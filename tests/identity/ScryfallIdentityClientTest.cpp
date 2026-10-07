#include <QtTest>

#include "identity/ScryfallIdentityClient.h"
#include "import/ImportFailure.h"

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace tcgprint::identity;
using namespace tcgprint::imports;

namespace {

const QByteArray ValidCardJson = QByteArrayLiteral(
    R"json({
        "id":"print-id-123",
        "oracle_id":"oracle-id-456",
        "name":"Sol Ring",
        "layout":"normal",
        "set":"cmm",
        "collector_number":"378",
        "lang":"en",
        "card_faces":[{"name":"Sol Ring"}],
        "all_parts":[]
    })json"
);

UrlFetchOptions fakeOptions(
    UrlPinnedRequestExecutorWithHeaders executor
)
{
    UrlFetchOptions options;
    options.resolveHost =
        [](const QString&) {
            return QStringList{
                QStringLiteral("93.184.216.34")
            };
        };
    options.requestExecutorWithHeaders = std::move(executor);
    return options;
}

UrlHttpResponse jsonResponse(
    const QByteArray& body = ValidCardJson
)
{
    return UrlHttpResponse{
        .status = 200,
        .headers = {
            {
                QStringLiteral("content-type"),
                QStringLiteral("application/json; charset=utf-8")
            },
            {
                QStringLiteral("content-length"),
                QString::number(body.size())
            },
        },
        .body = body,
    };
}

ScryfallIdentityLookupError captureError(
    const std::function<void()>& operation
)
{
    try {
        operation();
    } catch (const ScryfallIdentityLookupError& error) {
        return error;
    }
    throw std::runtime_error(
        "Expected Scryfall identity lookup to fail."
    );
}

} // namespace

class ScryfallIdentityClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void looksUpScryfallIdOnOfficialEndpointAndPreservesOracleId()
    {
        QUrl seenUrl;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&seenUrl](
                    const QUrl& url,
                    const QString&,
                    const UrlRequestHeaders&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenUrl = url;
                    return jsonResponse();
                }
            )
        );

        const auto card = client.lookupById(
            QStringLiteral("printing/ä")
        );

        QCOMPARE(
            seenUrl.toString(QUrl::FullyEncoded),
            QStringLiteral(
                "https://api.scryfall.com/cards/printing%2F%C3%A4"
            )
        );
        QCOMPARE(seenUrl.scheme(), QStringLiteral("https"));
        QCOMPARE(seenUrl.host(), QStringLiteral("api.scryfall.com"));
        QCOMPARE(card.id, QStringLiteral("print-id-123"));
        QVERIFY(card.oracleId.has_value());
        QCOMPARE(*card.oracleId, QStringLiteral("oracle-id-456"));
    }

    void looksUpSetAndCollectorWithoutLanguage()
    {
        QUrl seenUrl;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&seenUrl](
                    const QUrl& url,
                    const QString&,
                    const UrlRequestHeaders&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenUrl = url;
                    return jsonResponse();
                }
            )
        );

        static_cast<void>(client.lookupBySetCollector(
            QStringLiteral("neo"),
            QStringLiteral("123"),
            std::nullopt
        ));

        QCOMPARE(
            seenUrl.toString(QUrl::FullyEncoded),
            QStringLiteral("https://api.scryfall.com/cards/neo/123")
        );
    }

    void looksUpSetAndCollectorWithLanguageAndEncodedSegments()
    {
        QUrl seenUrl;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&seenUrl](
                    const QUrl& url,
                    const QString&,
                    const UrlRequestHeaders&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenUrl = url;
                    return jsonResponse();
                }
            )
        );

        static_cast<void>(client.lookupBySetCollector(
            QStringLiteral("neo"),
            QStringLiteral("12★/A"),
            QStringLiteral("ja")
        ));

        QCOMPARE(
            seenUrl.toString(QUrl::FullyEncoded),
            QStringLiteral(
                "https://api.scryfall.com/cards/neo/12%E2%98%85%2FA/ja"
            )
        );
    }

    void looksUpNameUsingExactQueryAndPercentEncoding()
    {
        QUrl seenUrl;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&seenUrl](
                    const QUrl& url,
                    const QString&,
                    const UrlRequestHeaders&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenUrl = url;
                    return jsonResponse();
                }
            )
        );

        static_cast<void>(client.lookupByExactName(
            QStringLiteral("Fire // Ice & Ice")
        ));

        QCOMPARE(
            seenUrl.toString(QUrl::FullyEncoded),
            QStringLiteral(
                "https://api.scryfall.com/cards/named?exact=Fire%20%2F%2F%20Ice%20%26%20Ice"
            )
        );
        QVERIFY(!seenUrl.query().contains(QStringLiteral("fuzzy=")));
    }

    void classifiesHttpFailureStatuses()
    {
        const std::vector<std::pair<int, ScryfallIdentityLookupErrorKind>> cases{
            {404, ScryfallIdentityLookupErrorKind::NotFound},
            {429, ScryfallIdentityLookupErrorKind::RateLimited},
            {500, ScryfallIdentityLookupErrorKind::Server},
            {503, ScryfallIdentityLookupErrorKind::Server},
            {400, ScryfallIdentityLookupErrorKind::Http},
        };

        for (const auto& [status, expectedKind] : cases) {
            const ScryfallIdentityClient client(
                fakeOptions(
                    [status](
                        const QUrl&,
                        const QString&,
                        const UrlRequestHeaders&,
                        std::uint64_t,
                        std::uint64_t
                    ) {
                        return UrlHttpResponse{
                            .status = status,
                            .headers = {
                                {
                                    QStringLiteral("content-type"),
                                    QStringLiteral("application/json")
                                },
                            },
                            .body = QByteArrayLiteral("private remote response"),
                        };
                    }
                )
            );

            const auto error = captureError([&client] {
                static_cast<void>(client.lookupById(
                    QStringLiteral("missing")
                ));
            });

            QVERIFY(error.kind() == expectedKind);
            QCOMPARE(error.status().value_or(0), status);
            QVERIFY(
                !QString::fromUtf8(error.what()).contains(
                    QStringLiteral("private remote response")
                )
            );
        }
    }

    void rejectsInvalidJsonAndInvalidUtf8()
    {
        const std::vector<QByteArray> invalidBodies{
            QByteArrayLiteral("{not json"),
            QByteArray::fromHex("7b22ff227d"),
        };

        for (const QByteArray& body : invalidBodies) {
            const ScryfallIdentityClient client(
                fakeOptions(
                    [&body](
                        const QUrl&,
                        const QString&,
                        const UrlRequestHeaders&,
                        std::uint64_t,
                        std::uint64_t
                    ) {
                        return jsonResponse(body);
                    }
                )
            );
            const auto error = captureError([&client] {
                static_cast<void>(client.lookupById(
                    QStringLiteral("invalid")
                ));
            });
            QVERIFY(
                error.kind()
                == ScryfallIdentityLookupErrorKind::InvalidJson
            );
        }
    }

    void rejectsStructurallyInvalidScryfallPayload()
    {
        const ScryfallIdentityClient client(
            fakeOptions(
                [](const QUrl&, const QString&,
                   const UrlRequestHeaders&, std::uint64_t,
                   std::uint64_t) {
                    return jsonResponse(
                        QByteArrayLiteral("{\"id\":\"only-id\"}")
                    );
                }
            )
        );

        const auto error = captureError([&client] {
            static_cast<void>(client.lookupById(
                QStringLiteral("invalid-card")
            ));
        });

        QVERIFY(
            error.kind()
            == ScryfallIdentityLookupErrorKind::InvalidPayload
        );
    }

    void rejectsNonJsonContentType()
    {
        const ScryfallIdentityClient client(
            fakeOptions(
                [](const QUrl&, const QString&,
                   const UrlRequestHeaders&, std::uint64_t,
                   std::uint64_t) {
                    auto response = jsonResponse();
                    response.headers.insert(
                        QStringLiteral("content-type"),
                        QStringLiteral("text/html")
                    );
                    return response;
                }
            )
        );

        const auto error = captureError([&client] {
            static_cast<void>(client.lookupById(
                QStringLiteral("html")
            ));
        });

        QVERIFY(
            error.kind()
            == ScryfallIdentityLookupErrorKind::InvalidContentType
        );
    }

    void rejectsRedirectWithoutRequestingAnotherHost()
    {
        int calls = 0;
        QUrl firstUrl;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&calls, &firstUrl](
                    const QUrl& url,
                    const QString&,
                    const UrlRequestHeaders&,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    ++calls;
                    firstUrl = url;
                    return UrlHttpResponse{
                        .status = 302,
                        .headers = {
                            {
                                QStringLiteral("location"),
                                QStringLiteral("http://evil.invalid/card")
                            },
                        },
                    };
                }
            )
        );

        const auto error = captureError([&client] {
            static_cast<void>(client.lookupById(
                QStringLiteral("https://evil.invalid/credential")
            ));
        });

        QCOMPARE(calls, 1);
        QCOMPARE(firstUrl.scheme(), QStringLiteral("https"));
        QCOMPARE(firstUrl.host(), QStringLiteral("api.scryfall.com"));
        QVERIFY(firstUrl.userName().isEmpty());
        QVERIFY(
            error.kind()
            == ScryfallIdentityLookupErrorKind::Network
        );
    }

    void classifiesTransportTimeoutAndNetworkFailure()
    {
        const ScryfallIdentityClient timeoutClient(
            fakeOptions(
                [](const QUrl&, const QString&,
                   const UrlRequestHeaders&, std::uint64_t,
                   std::uint64_t) -> UrlHttpResponse {
                    throw ImportFailureError(
                        QStringLiteral("remote timeout"),
                        QStringLiteral("URL_TIMEOUT")
                    );
                }
            )
        );
        const auto timeout = captureError([&timeoutClient] {
            static_cast<void>(timeoutClient.lookupById(
                QStringLiteral("slow")
            ));
        });
        QVERIFY(
            timeout.kind()
            == ScryfallIdentityLookupErrorKind::Timeout
        );

        const ScryfallIdentityClient networkClient(
            fakeOptions(
                [](const QUrl&, const QString&,
                   const UrlRequestHeaders&, std::uint64_t,
                   std::uint64_t) -> UrlHttpResponse {
                    throw std::runtime_error("remote network detail");
                }
            )
        );
        const auto network = captureError([&networkClient] {
            static_cast<void>(networkClient.lookupById(
                QStringLiteral("offline")
            ));
        });
        QVERIFY(
            network.kind()
            == ScryfallIdentityLookupErrorKind::Network
        );
        QVERIFY(
            !QString::fromUtf8(network.what()).contains(
                QStringLiteral("remote network detail")
            )
        );
    }

    void sendsIdentifiableUserAgentAndJsonAcceptHeader()
    {
        UrlRequestHeaders seenHeaders;
        const ScryfallIdentityClient client(
            fakeOptions(
                [&seenHeaders](
                    const QUrl&,
                    const QString&,
                    const UrlRequestHeaders& headers,
                    std::uint64_t,
                    std::uint64_t
                ) {
                    seenHeaders = headers;
                    return jsonResponse();
                }
            )
        );

        static_cast<void>(client.lookupById(QStringLiteral("print-id")));

        QVERIFY(
            seenHeaders.value(QStringLiteral("user-agent"))
                .startsWith(QStringLiteral("TCGPrint/"))
        );
        QCOMPARE(
            seenHeaders.value(QStringLiteral("accept")),
            QStringLiteral("application/json;q=0.9,*/*;q=0.8")
        );
    }

    void enforcesResponseSizeLimit()
    {
        UrlFetchOptions options = fakeOptions(
            [](const QUrl&, const QString&,
               const UrlRequestHeaders&, std::uint64_t,
               std::uint64_t) {
                return jsonResponse(QByteArray(32, 'x'));
            }
        );
        options.policy.maxResponseBytes = 16;
        const ScryfallIdentityClient client(options);

        const auto error = captureError([&client] {
            static_cast<void>(client.lookupById(
                QStringLiteral("large")
            ));
        });

        QVERIFY(
            error.kind()
            == ScryfallIdentityLookupErrorKind::ResponseTooLarge
        );
    }
};

QTEST_APPLESS_MAIN(ScryfallIdentityClientTest)

#include "ScryfallIdentityClientTest.moc"
