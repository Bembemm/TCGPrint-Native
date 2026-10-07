#include <QtTest>

#include <algorithm>

#include "import/ImportFailure.h"
#include "import/XmlImport.h"

using namespace tcgprint::imports;

namespace {

QByteArray encode(const QString& xml)
{
    return xml.toUtf8();
}

ImportSource xmlSource(
    const QString& filename,
    const QString& xml
)
{
    const QByteArray bytes = xml.toUtf8();
    return ImportSource{
        .id = filename,
        .kind = ImportSourceKind::File,
        .filename = filename,
        .order = 0,
        .sizeBytes =
            static_cast<std::uint64_t>(bytes.size()),
        .originalBytes = bytes,
    };
}

} // namespace

class XmlImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void parsesWellFormedXmlWithoutRewritingSource()
    {
        const QByteArray bytes = encode(
            QStringLiteral(
                "<deck><card name=\"Sol Ring\">1</card></deck>"
            )
        );
        const QByteArray original = bytes;

        const SafeXmlDocument document =
            parseSafeXml(bytes);

        QCOMPARE(
            document.root.name,
            QStringLiteral("deck")
        );
        QCOMPARE(document.root.children.size(), std::size_t{1});
        QCOMPARE(
            document.root.children.front()
                .attributes.value(QStringLiteral("name"))
                .toString(),
            QStringLiteral("Sol Ring")
        );
        QCOMPARE(bytes, original);
    }

    void blocksDtdAndEntityDeclarations()
    {
        try {
            static_cast<void>(
                parseSafeXml(
                    encode(
                        QStringLiteral(
                            "<!DOCTYPE order [<!ENTITY local SYSTEM \"file:///etc/passwd\">]><order>&local;</order>"
                        )
                    )
                )
            );
            QFAIL("Expected DTD rejection.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("XML_DTD_BLOCKED")
            );
        }
    }

    void rejectsMalformedAndUndeclaredEntities()
    {
        for (
            const QByteArray& input :
            {
                encode(
                    QStringLiteral("<deck><card></deck>")
                ),
                encode(
                    QStringLiteral("<deck>&external;</deck>")
                )
            }
        ) {
            try {
                static_cast<void>(parseSafeXml(input));
                QFAIL("Expected invalid XML.");
            } catch (const ImportFailureError& error) {
                QCOMPARE(
                    error.code(),
                    QStringLiteral("INVALID_XML")
                );
            }
        }
    }

    void enforcesByteDepthAndNodeLimits()
    {
        ImportLimitOverrides byteLimit;
        byteLimit.maxXmlBytes = 4;
        try {
            static_cast<void>(
                parseSafeXml(
                    encode(QStringLiteral("<deck />")),
                    byteLimit
                )
            );
            QFAIL("Expected byte limit.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INPUT_TOO_LARGE")
            );
        }

        ImportLimitOverrides depthLimit;
        depthLimit.maxXmlDepth = 2;
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                parseSafeXml(
                    encode(
                        QStringLiteral("<a><b><c /></b></a>")
                    ),
                    depthLimit
                )
            ),
            ImportFailureError
        );

        ImportLimitOverrides nodeLimit;
        nodeLimit.maxXmlNodes = 2;
        QVERIFY_EXCEPTION_THROWN(
            static_cast<void>(
                parseSafeXml(
                    encode(
                        QStringLiteral("<a><b/><c/></a>")
                    ),
                    nodeLimit
                )
            ),
            ImportFailureError
        );
    }

    void genericXmlRetainsRootMetadata()
    {
        const ImporterOutput output = importGenericXml(
            xmlSource(
                QStringLiteral("deck.xml"),
                QStringLiteral(
                    "<deck version=\"1\"><card /></deck>"
                )
            )
        );

        QCOMPARE(output.entries.size(), std::size_t{1});
        QCOMPARE(
            static_cast<int>(output.entries.front().kind),
            static_cast<int>(ImportedEntryKind::Document)
        );
        QCOMPARE(
            output.entries.front()
                .metadata.value(QStringLiteral("rootName"))
                .toString(),
            QStringLiteral("deck")
        );
        QCOMPARE(
            output.entries.front()
                .metadata
                .value(QStringLiteral("rootAttributes"))
                .toObject()
                .value(QStringLiteral("version"))
                .toString(),
            QStringLiteral("1")
        );
    }

    void importsMpcFrontBackSlotsAndArtworkIds()
    {
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<order>"
            "<details><quantity>1</quantity><bracket>standard</bracket></details>"
            "<fronts>"
            "<card><id>synthetic-front-art-a</id><slots>2, 1</slots><quantity>9</quantity><name>Example Front</name><query>synthetic:front-a</query></card>"
            "<card><id>synthetic-front-art-a</id><slot>3</slot><name>Repeated Example Front</name></card>"
            "<card><slots>4</slots><name>Optional ID Example</name></card>"
            "</fronts>"
            "<backs>"
            "<card><id>synthetic-back-art-a</id><slots>1,2</slots><name>Example Back</name><query>synthetic:back-a</query></card>"
            "<card><id>synthetic-back-art-b</id><slot>3</slot><name>Repeated Example Back</name></card>"
            "<card><id>synthetic-back-art-c</id><slots>4</slots></card>"
            "</backs>"
            "<cardback>synthetic-cardback-artwork</cardback>"
            "</order>"
        );

        ImportSource source = xmlSource(
            QStringLiteral("order.xml"),
            xml
        );
        source.order = 7;

        const ImporterOutput result =
            importMpcAutofillXml(source);

        QCOMPARE(result.entries.size(), std::size_t{3});
        QCOMPARE(
            result.entries[0].cardHint->name.value_or(QString()),
            QStringLiteral("Example Front")
        );
        QCOMPARE(
            result.entries[1].cardHint->name.value_or(QString()),
            QStringLiteral("Repeated Example Front")
        );
        QCOMPARE(
            result.entries[2].cardHint->name.value_or(QString()),
            QStringLiteral("Optional ID Example")
        );

        QCOMPARE(result.entries[0].quantity, 2ULL);
        QCOMPARE(result.entries[1].quantity, 1ULL);
        QCOMPARE(result.entries[2].quantity, 1ULL);

        QCOMPARE(
            static_cast<int>(result.entries[0].kind),
            static_cast<int>(ImportedEntryKind::MpcOrderCard)
        );
        QCOMPARE(
            result.entries[0].slots,
            QStringList({QStringLiteral("2"), QStringLiteral("1")})
        );
        QVERIFY(result.entries[0].front.has_value());
        QCOMPARE(
            result.entries[0]
                .front->selectedArtworkId.value_or(QString()),
            QStringLiteral("synthetic-front-art-a")
        );
        QCOMPARE(
            result.entries[0].front->name.value_or(QString()),
            QStringLiteral("Example Front")
        );
        QCOMPARE(
            result.entries[0].front->query.value_or(QString()),
            QStringLiteral("synthetic:front-a")
        );

        QCOMPARE(result.entries[0].faces.size(), std::size_t{2});
        QCOMPARE(
            static_cast<int>(result.entries[0].faces[0].side),
            static_cast<int>(ImportedFaceSide::Front)
        );
        QCOMPARE(
            static_cast<int>(result.entries[0].faces[1].side),
            static_cast<int>(ImportedFaceSide::Back)
        );
        QCOMPARE(
            result.entries[0].faces[1]
                .selectedArtworkId.value_or(QString()),
            QStringLiteral("synthetic-back-art-a")
        );

        QCOMPARE(
            result.entries[0].faceAssociations.size(),
            std::size_t{2}
        );
        QVERIFY(
            result.entries[0].faceAssociations[0]
                .backAssetId.has_value()
        );
        QVERIFY(
            result.entries[0].faceAssociations[1]
                .backAssetId.has_value()
        );

        QVERIFY(result.entries[1].back.has_value());
        QCOMPARE(
            result.entries[1]
                .back->selectedArtworkId.value_or(QString()),
            QStringLiteral("synthetic-back-art-b")
        );
        QVERIFY(
            !result.entries[2]
                .front->selectedArtworkId.has_value()
        );
        QCOMPARE(
            result.entries[2]
                .metadata.value(QStringLiteral("cardback"))
                .toString(),
            QStringLiteral("synthetic-cardback-artwork")
        );
        QCOMPARE(
            result.metadata
                .value(QStringLiteral("cardbackAsset"))
                .toObject()
                .value(QStringLiteral("selectedArtworkId"))
                .toString(),
            QStringLiteral("synthetic-cardback-artwork")
        );

        QVERIFY(
            std::any_of(
                result.warnings.begin(),
                result.warnings.end(),
                [](const ImportWarning& warning) {
                    return warning.code
                        == QStringLiteral(
                            "MPC_QUANTITY_DIFFERS_FROM_SLOTS"
                        );
                }
            )
        );
    }

    void retainsMpcDetailsAndCardbackWithoutCards()
    {
        const ImporterOutput result =
            importMpcAutofillXml(
                xmlSource(
                    QStringLiteral("empty-order.xml"),
                    QStringLiteral(
                        "<order>"
                        "<details><quantity>1</quantity></details>"
                        "<cardback>synthetic-back</cardback>"
                        "</order>"
                    )
                )
            );

        QCOMPARE(result.entries.size(), std::size_t{1});
        QCOMPARE(
            static_cast<int>(result.entries[0].kind),
            static_cast<int>(ImportedEntryKind::Document)
        );
        QCOMPARE(
            result.entries[0]
                .metadata.value(QStringLiteral("cardback"))
                .toString(),
            QStringLiteral("synthetic-back")
        );
        QCOMPARE(
            result.metadata
                .value(QStringLiteral("cardbackAsset"))
                .toObject()
                .value(QStringLiteral("selectedArtworkId"))
                .toString(),
            QStringLiteral("synthetic-back")
        );
    }

    void rejectsMalformedAndWrongRootMpcXml()
    {
        for (
            const QString& xml :
            {
                QStringLiteral("<order><fronts>"),
                QStringLiteral("<deck><card /></deck>")
            }
        ) {
            try {
                static_cast<void>(
                    importMpcAutofillXml(
                        xmlSource(
                            QStringLiteral("bad-order.xml"),
                            xml
                        )
                    )
                );
                QFAIL("Expected MPC XML failure.");
            } catch (const ImportFailureError& error) {
                QVERIFY(
                    error.code() == QStringLiteral("INVALID_XML")
                    || error.code() == QStringLiteral("FORMAT_MISMATCH")
                );
            }
        }
    }

};

QTEST_APPLESS_MAIN(XmlImportTest)

#include "XmlImportTest.moc"
