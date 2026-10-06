#include <QtTest>

#include "import/ImportFailure.h"
#include "import/JsonImport.h"

using namespace tcgprint::imports;

namespace {

ImportSource source(
    const QString& filename,
    const QString& text
)
{
    const QByteArray encoded = text.toUtf8();
    return ImportSource{
        .id = filename,
        .kind = ImportSourceKind::File,
        .filename = filename,
        .order = 0,
        .sizeBytes =
            static_cast<std::uint64_t>(encoded.size()),
        .originalBytes = encoded,
    };
}

} // namespace

class JsonImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void recognizesCommonCardsArraysAndAliases()
    {
        const ImporterOutput result = parseJsonImport(
            source(
                QStringLiteral("deck.json"),
                QStringLiteral(
                    R"({"cards":[{"name":"Sol Ring","count":2,"set_code":"CMM"}]})"
                )
            )
        );

        QCOMPARE(result.entries.size(), std::size_t{1});
        QCOMPARE(result.entries.front().quantity, 2ULL);
        QCOMPARE(
            result.entries.front()
                .cardHint->name.value_or(QString()),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(
            result.entries.front()
                .cardHint->setCode.value_or(QString()),
            QStringLiteral("CMM")
        );
    }

    void supportsExplicitNestedFieldPaths()
    {
        JsonImportMapping mapping{
            .name = QStringLiteral("cards[].print.label"),
            .quantity = QStringLiteral("cards[].copies"),
            .scryfallId = QStringLiteral("cards[].print.id"),
            .imageUrl = QStringLiteral("cards[].image"),
        };

        const ImporterOutput result = parseJsonImport(
            source(
                QStringLiteral("custom.json"),
                QStringLiteral(
                    R"({"cards":[{"print":{"label":"Island","id":"s1"},"copies":4,"image":"local.png"}]})"
                )
            ),
            mapping
        );

        QCOMPARE(result.entries.size(), std::size_t{1});
        QCOMPARE(result.entries.front().quantity, 4ULL);
        QCOMPARE(
            result.entries.front()
                .cardHint->name.value_or(QString()),
            QStringLiteral("Island")
        );
        QCOMPARE(
            result.entries.front()
                .cardHint->scryfallId.value_or(QString()),
            QStringLiteral("s1")
        );
        QCOMPARE(
            result.entries.front()
                .cardHint->imageUrl.value_or(QString()),
            QStringLiteral("local.png")
        );
        QCOMPARE(
            result.mappings.front()
                .fields.value(QStringLiteral("name")).toString(),
            QStringLiteral("cards[].print.label")
        );
    }

    void returnsTypedErrorsForInvalidAndBoundedJson()
    {
        try {
            static_cast<void>(
                parseJsonImport(
                    source(
                        QStringLiteral("bad.json"),
                        QStringLiteral("{")
                    )
                )
            );
            QFAIL("Expected invalid JSON.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INVALID_JSON")
            );
        }

        ImportLimitOverrides depthLimit;
        depthLimit.maxJsonDepth = 3;
        try {
            static_cast<void>(
                parseJsonImport(
                    source(
                        QStringLiteral("deep.json"),
                        QStringLiteral(
                            R"({"cards":[{"name":"Sol Ring","extra":{"a":{"b":1}}}]})"
                        )
                    ),
                    std::nullopt,
                    depthLimit
                )
            );
            QFAIL("Expected depth limit failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INVALID_JSON")
            );
        }

        ImportLimitOverrides nodeLimit;
        nodeLimit.maxJsonNodes = 2;
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                parseJsonImport(
                    source(
                        QStringLiteral("nodes.json"),
                        QStringLiteral(
                            R"({"cards":[{"name":"Sol Ring"}]})"
                        )
                    ),
                    std::nullopt,
                    nodeLimit
                )
            ),
            ImportFailureError
        );
    }

    void warnsWhenNoCardCollectionExists()
    {
        const ImporterOutput result = parseJsonImport(
            source(
                QStringLiteral("empty.json"),
                QStringLiteral(R"({"unrelated":true})")
            )
        );

        QVERIFY(result.entries.empty());
        QCOMPARE(result.warnings.size(), std::size_t{1});
        QCOMPARE(
            result.warnings.front().code,
            QStringLiteral("NO_CARD_COLLECTION")
        );
    }
};

QTEST_APPLESS_MAIN(JsonImportTest)

#include "JsonImportTest.moc"
