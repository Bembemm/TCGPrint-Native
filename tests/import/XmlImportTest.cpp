#include <QtTest>

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
};

QTEST_APPLESS_MAIN(XmlImportTest)

#include "XmlImportTest.moc"
