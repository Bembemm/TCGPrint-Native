#include <QtTest>

#include "import/ImportFailure.h"
#include "import/UrlTransportPolicy.h"

#include <stdexcept>

using namespace tcgprint::imports;

class UrlTransportPolicyTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesCurrentTransportDefaults()
    {
        const UrlTransportPolicy policy;
        QCOMPARE(policy.timeoutMs, 10'000ULL);
        QCOMPARE(
            policy.maxResponseBytes,
            25ULL * 1024ULL * 1024ULL
        );
        QCOMPARE(policy.maxRedirects, 5);
        QVERIFY_EXCEPTION_THROWN(
            validateUrlTransportPolicy(
                UrlTransportPolicy{
                    .timeoutMs = 0,
                }
            ),
            std::invalid_argument
        );
    }

    void sanitizesSensitiveReportUrls()
    {
        QCOMPARE(
            sanitizeUrlForReport(
                QStringLiteral(
                    "https://user:pass@files.example.com/list?token=top-secret&download=1#frag"
                )
            ),
            QStringLiteral(
                "https://files.example.com/list?token=%5Bredacted%5D&download=1"
            )
        );

        QCOMPARE(
            sanitizeUrlForReport(
                QStringLiteral("not a url")
            ),
            QStringLiteral("[invalid URL]")
        );
    }

    void classifiesPublicAndReservedIpv4()
    {
        QVERIFY(
            isPublicUrlAddress(
                QStringLiteral("93.184.216.34")
            )
        );

        for (const QString& address : {
            QStringLiteral("127.0.0.1"),
            QStringLiteral("10.0.0.4"),
            QStringLiteral("169.254.1.1"),
            QStringLiteral("172.16.0.1"),
            QStringLiteral("192.168.1.1"),
            QStringLiteral("192.0.2.1"),
            QStringLiteral("198.51.100.4"),
            QStringLiteral("203.0.113.8"),
            QStringLiteral("224.0.0.1"),
        }) {
            QVERIFY(!isPublicUrlAddress(address));
        }
    }

    void classifiesIpv6AndMappedIpv4()
    {
        QVERIFY(
            isPublicUrlAddress(
                QStringLiteral("2606:4700:4700::1111")
            )
        );

        for (const QString& address : {
            QStringLiteral("::1"),
            QStringLiteral("fe80::1"),
            QStringLiteral("2001:db8::1"),
            QStringLiteral("::ffff:127.0.0.1"),
            QStringLiteral("::ffff:192.0.2.1"),
        }) {
            QVERIFY(!isPublicUrlAddress(address));
        }

        QVERIFY(
            isPublicUrlAddress(
                QStringLiteral("::ffff:93.184.216.34")
            )
        );
    }

    void blocksPrivateDirectAndDnsTargets()
    {
        try {
            validateResolvedUrlHost(
                QUrl(
                    QStringLiteral(
                        "http://192.0.2.1/deck.txt"
                    )
                ),
                {}
            );
            QFAIL("Expected URL_HOST_BLOCKED.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_HOST_BLOCKED")
            );
        }

        try {
            validateResolvedUrlHost(
                QUrl(
                    QStringLiteral(
                        "https://files.example.invalid/deck.txt"
                    )
                ),
                QStringList{
                    QStringLiteral("10.0.0.4")
                }
            );
            QFAIL("Expected URL_HOST_BLOCKED.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_HOST_BLOCKED")
            );
        }

        QVERIFY_NO_THROW(
            validateResolvedUrlHost(
                QUrl(
                    QStringLiteral(
                        "https://files.example.invalid/deck.txt"
                    )
                ),
                QStringList{
                    QStringLiteral("93.184.216.34")
                }
            )
        );
    }

    void blocksLocalHostNames()
    {
        for (const QString& host : {
            QStringLiteral("localhost"),
            QStringLiteral("app.localhost"),
            QStringLiteral("printer.local"),
            QStringLiteral("service.internal"),
        }) {
            try {
                validateResolvedUrlHost(
                    QUrl(
                        QStringLiteral("https://")
                        + host
                        + QStringLiteral("/deck.txt")
                    ),
                    QStringList{
                        QStringLiteral("93.184.216.34")
                    }
                );
                QFAIL("Expected URL_HOST_BLOCKED.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_HOST_BLOCKED")
                );
            }
        }
    }

    void validatesRedirectTargetsAndAddresses()
    {
        QVERIFY_NO_THROW(
            validateUrlRedirectTarget(
                QUrl(
                    QStringLiteral(
                        "https://files.example.com/deck.txt"
                    )
                )
            )
        );

        for (const QString& value : {
            QStringLiteral("file:///tmp/deck.txt"),
            QStringLiteral(
                "https://user:pass@files.example.com/deck.txt"
            ),
        }) {
            try {
                validateUrlRedirectTarget(QUrl(value));
                QFAIL("Expected URL_REDIRECT_BLOCKED.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("URL_REDIRECT_BLOCKED")
                );
            }
        }

        try {
            validateResolvedUrlHost(
                QUrl(
                    QStringLiteral(
                        "http://127.0.0.1/admin"
                    )
                ),
                {},
                true
            );
            QFAIL("Expected redirect block.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_REDIRECT_BLOCKED")
            );
        }
    }

    void enforcesRedirectAndResponseBounds()
    {
        QVERIFY_NO_THROW(
            validateUrlRedirectCount(4, 5)
        );
        QVERIFY_EXCEPTION_THROWN(
            validateUrlRedirectCount(5, 5),
            ImportFailureError
        );

        QVERIFY_NO_THROW(
            validateUrlResponseSize(
                100,
                100,
                100
            )
        );

        try {
            validateUrlResponseSize(
                101,
                0,
                100
            );
            QFAIL("Expected URL_RESPONSE_LIMIT.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_RESPONSE_LIMIT")
            );
        }

        QVERIFY_EXCEPTION_THROWN(
            validateUrlResponseSize(
                std::nullopt,
                101,
                100
            ),
            ImportFailureError
        );
    }
};

QTEST_APPLESS_MAIN(UrlTransportPolicyTest)

#include "UrlTransportPolicyTest.moc"
