#include <QtTest>
#include <QJsonDocument>

#include "import/DirectFileUrlImport.h"
#include "import/ImportFailure.h"

#include <algorithm>

using namespace tcgprint::imports;

namespace {

UrlFetchOptions payloadOptions(
    UrlHttpResponse response
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
        [response = std::move(response)](
            const QUrl&,
            const QString&,
            std::uint64_t,
            std::uint64_t
        ) {
            return response;
        };
    return options;
}

bool hasWarning(
    const DirectFileUrlImportResult& result,
    const QString& code
)
{
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [&code](const ImportWarning& warning) {
            return warning.code == code;
        }
    );
}

} // namespace

class DirectFileUrlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void buildsDetectedUrlSource()
    {
        const auto result = importDirectFileUrl(
            QStringLiteral(
                "https://files.example.invalid/deck.csv"
            ),
            QStringLiteral("source:1"),
            7,
            payloadOptions(
                UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-type"),
                            QStringLiteral("text/csv")
                        },
                    },
                    .body = QByteArrayLiteral(
                        "name,quantity\nSol Ring,2\n"
                    ),
                }
            )
        );

        QCOMPARE(
            static_cast<int>(result.source.kind),
            static_cast<int>(ImportSourceKind::Url)
        );
        QCOMPARE(
            result.source.filename.value_or(QString()),
            QStringLiteral("deck.csv")
        );
        QCOMPARE(
            result.source.mediaType.value_or(QString()),
            QStringLiteral("text/csv")
        );
        QCOMPARE(result.source.order, 7);
        QCOMPARE(
            result.source.sha256.value_or(QString()).size(),
            64
        );
        QVERIFY(result.source.originalBytes.has_value());
        QVERIFY(
            std::any_of(
                result.detection.candidates.begin(),
                result.detection.candidates.end(),
                [](const ImportCandidate& candidate) {
                    return candidate.kind
                        == ImportKind::Csv;
                }
            )
        );
        QVERIFY(result.warnings.empty());
    }

    void prefersSafeContentDispositionFilename()
    {
        const auto result = importDirectFileUrl(
            QStringLiteral(
                "https://files.example.invalid/download"
            ),
            QStringLiteral("source:2"),
            0,
            payloadOptions(
                UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-type"),
                            QStringLiteral("text/plain")
                        },
                        {
                            QStringLiteral("content-disposition"),
                            QStringLiteral(
                                "attachment; filename=\"../deck.txt\""
                            )
                        },
                    },
                    .body = QByteArrayLiteral("1 Sol Ring"),
                }
            )
        );

        QCOMPARE(
            result.source.filename.value_or(QString()),
            QStringLiteral("deck.txt")
        );
    }

    void rejectsHtmlRegardlessOfFilename()
    {
        try {
            static_cast<void>(
                importDirectFileUrl(
                    QStringLiteral(
                        "https://files.example.invalid/deck.txt"
                    ),
                    QStringLiteral("source:html"),
                    0,
                    payloadOptions(
                        UrlHttpResponse{
                            .status = 200,
                            .headers = {
                                {
                                    QStringLiteral("content-type"),
                                    QStringLiteral("text/plain")
                                },
                            },
                            .body = QByteArrayLiteral(
                                "<!doctype html><html></html>"
                            ),
                        }
                    )
                )
            );
            QFAIL("Expected URL_CONTENT_TYPE.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_CONTENT_TYPE")
            );
        }
    }

    void rejectsUnsupportedMediaType()
    {
        try {
            static_cast<void>(
                importDirectFileUrl(
                    QStringLiteral(
                        "https://files.example.invalid/data"
                    ),
                    QStringLiteral("source:media"),
                    0,
                    payloadOptions(
                        UrlHttpResponse{
                            .status = 200,
                            .headers = {
                                {
                                    QStringLiteral("content-type"),
                                    QStringLiteral("application/pdf")
                                },
                            },
                            .body = QByteArrayLiteral("%PDF"),
                        }
                    )
                )
            );
            QFAIL("Expected URL_CONTENT_TYPE.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_CONTENT_TYPE")
            );
        }
    }

    void trustsImageBytesOverConflictingMime()
    {
        QByteArray png;
        png.append(
            QByteArray::fromHex(
                "89504e470d0a1a0a"
            )
        );
        png.append(QByteArray(32, '\0'));

        const auto result = importDirectFileUrl(
            QStringLiteral(
                "https://files.example.invalid/card.csv"
            ),
            QStringLiteral("source:png"),
            0,
            payloadOptions(
                UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-type"),
                            QStringLiteral("text/csv")
                        },
                    },
                    .body = png,
                }
            )
        );

        const auto image = std::find_if(
            result.detection.candidates.begin(),
            result.detection.candidates.end(),
            [](const ImportCandidate& candidate) {
                return candidate.kind
                    == ImportKind::Image;
            }
        );
        QVERIFY(
            image != result.detection.candidates.end()
        );
        QCOMPARE(
            image->originalFormat.value_or(QString()),
            QStringLiteral("png")
        );
        QVERIFY(
            hasWarning(
                result,
                QStringLiteral(
                    "URL_CONTENT_TYPE_MISMATCH"
                )
            )
        );
        QVERIFY(
            result.source.metadata
                .value(
                    QStringLiteral(
                        "contentTypeMismatch"
                    )
                )
                .toBool()
        );
    }

    void redactsSecretsFromStoredUrls()
    {
        const auto result = importDirectFileUrl(
            QStringLiteral(
                "https://files.example.invalid/deck.txt?token=top-secret&download=1"
            ),
            QStringLiteral("source:secret"),
            0,
            payloadOptions(
                UrlHttpResponse{
                    .status = 200,
                    .headers = {
                        {
                            QStringLiteral("content-type"),
                            QStringLiteral("text/plain")
                        },
                    },
                    .body = QByteArrayLiteral("1 Sol Ring"),
                }
            )
        );

        const QString serialized =
            QString::fromUtf8(
                QJsonDocument(
                    result.source.metadata
                ).toJson(
                    QJsonDocument::Compact
                )
            )
            + result.source.sourceUrl.value_or(QString());

        QVERIFY(!serialized.contains(
            QStringLiteral("top-secret")
        ));
        QVERIFY(serialized.contains(
            QStringLiteral("%5Bredacted%5D")
        ));
    }

    void rejectsUnknownPayload()
    {
        try {
            static_cast<void>(
                importDirectFileUrl(
                    QStringLiteral(
                        "https://files.example.invalid/blob.bin"
                    ),
                    QStringLiteral("source:unknown"),
                    0,
                    payloadOptions(
                        UrlHttpResponse{
                            .status = 200,
                            .headers = {
                                {
                                    QStringLiteral("content-type"),
                                    QStringLiteral(
                                        "application/octet-stream"
                                    )
                                },
                            },
                            .body = QByteArray::fromHex(
                                "fffefd"
                            ),
                        }
                    )
                )
            );
            QFAIL("Expected URL_CONTENT_TYPE.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("URL_CONTENT_TYPE")
            );
        }
    }
};

QTEST_APPLESS_MAIN(DirectFileUrlImportTest)

#include "DirectFileUrlImportTest.moc"
