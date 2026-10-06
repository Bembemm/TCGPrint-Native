#include "import/XmlImport.h"

#include "import/ImportFailure.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>

namespace tcgprint::imports {
namespace {

QString decodeXmlBytes(const QByteArray& input)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(input);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral("XML input must be valid UTF-8."),
            QStringLiteral("INVALID_XML")
        );
    }
    return text;
}

void incrementNode(
    std::uint64_t depth,
    std::uint64_t& nodeCount,
    std::uint64_t& maxDepth,
    const ImportLimits& limits
)
{
    ++nodeCount;
    maxDepth = std::max(maxDepth, depth);
    if (nodeCount > limits.maxXmlNodes) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured node limit."),
            QStringLiteral("INVALID_XML")
        );
    }
    if (depth > limits.maxXmlDepth) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured depth limit."),
            QStringLiteral("INVALID_XML")
        );
    }
}

SafeXmlNode parseElement(
    QXmlStreamReader& reader,
    std::uint64_t depth,
    std::uint64_t& nodeCount,
    std::uint64_t& maxDepth,
    const ImportLimits& limits
)
{
    incrementNode(
        depth,
        nodeCount,
        maxDepth,
        limits
    );

    SafeXmlNode node;
    node.name = reader.name().toString();
    for (const QXmlStreamAttribute& attribute : reader.attributes()) {
        node.attributes.insert(
            attribute.name().toString(),
            attribute.value().toString()
        );
    }

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token =
            reader.readNext();

        switch (token) {
        case QXmlStreamReader::StartElement:
            node.children.push_back(
                parseElement(
                    reader,
                    depth + 1,
                    nodeCount,
                    maxDepth,
                    limits
                )
            );
            break;

        case QXmlStreamReader::Characters:
            if (!reader.text().isEmpty()) {
                incrementNode(
                    depth + 1,
                    nodeCount,
                    maxDepth,
                    limits
                );
                node.text += reader.text().toString();
            }
            break;

        case QXmlStreamReader::Comment:
        case QXmlStreamReader::ProcessingInstruction:
            incrementNode(
                depth + 1,
                nodeCount,
                maxDepth,
                limits
            );
            break;

        case QXmlStreamReader::DTD:
            throw ImportFailureError(
                QStringLiteral(
                    "XML DTD and entity declarations are blocked."
                ),
                QStringLiteral("XML_DTD_BLOCKED")
            );

        case QXmlStreamReader::EndElement:
            return node;

        case QXmlStreamReader::Invalid:
            throw ImportFailureError(
                QStringLiteral("XML is not well formed."),
                QStringLiteral("INVALID_XML")
            );

        default:
            break;
        }
    }

    throw ImportFailureError(
        QStringLiteral("XML is not well formed."),
        QStringLiteral("INVALID_XML")
    );
}

SafeXmlDocument parseSafeXmlText(
    QString text,
    std::uint64_t byteLength,
    const ImportLimitOverrides& limitOverrides
)
{
    const ImportLimits limits =
        resolveImportLimits(limitOverrides);
    if (byteLength > limits.maxXmlBytes) {
        throw ImportFailureError(
            QStringLiteral("XML exceeds the configured byte limit."),
            QStringLiteral("INPUT_TOO_LARGE")
        );
    }

    if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
        text.remove(0, 1);
    }

    const QRegularExpression blockedDeclaration(
        QStringLiteral(R"(<!s*(?:DOCTYPE|ENTITY))"),
        QRegularExpression::CaseInsensitiveOption
    );
    if (blockedDeclaration.match(text).hasMatch()) {
        throw ImportFailureError(
            QStringLiteral(
                "XML DTD and entity declarations are blocked."
            ),
            QStringLiteral("XML_DTD_BLOCKED")
        );
    }

    QXmlStreamReader reader(text);
    std::uint64_t nodeCount = 0;
    std::uint64_t maxDepth = 0;
    std::optional<SafeXmlNode> root;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token =
            reader.readNext();

        if (token == QXmlStreamReader::DTD) {
            throw ImportFailureError(
                QStringLiteral(
                    "XML DTD and entity declarations are blocked."
                ),
                QStringLiteral("XML_DTD_BLOCKED")
            );
        }
        if (token == QXmlStreamReader::StartElement) {
            if (root.has_value()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "XML contains multiple root elements."
                    ),
                    QStringLiteral("INVALID_XML")
                );
            }
            root = parseElement(
                reader,
                1,
                nodeCount,
                maxDepth,
                limits
            );
        }
        if (token == QXmlStreamReader::Invalid) {
            break;
        }
    }

    if (
        reader.hasError()
        || !root.has_value()
    ) {
        throw ImportFailureError(
            QStringLiteral("XML is not well formed."),
            QStringLiteral("INVALID_XML")
        );
    }

    return SafeXmlDocument{
        .root = std::move(*root),
        .nodeCount = nodeCount,
        .maxDepth = maxDepth,
    };
}

} // namespace

SafeXmlDocument parseSafeXml(
    const QByteArray& input,
    const ImportLimitOverrides& limitOverrides
)
{
    return parseSafeXmlText(
        decodeXmlBytes(input),
        static_cast<std::uint64_t>(input.size()),
        limitOverrides
    );
}

SafeXmlDocument parseSafeXml(
    const QString& input,
    const ImportLimitOverrides& limitOverrides
)
{
    const QByteArray utf8 = input.toUtf8();
    return parseSafeXmlText(
        input,
        static_cast<std::uint64_t>(utf8.size()),
        limitOverrides
    );
}

ImporterOutput importGenericXml(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides
)
{
    if (!source.originalBytes.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "XML import requires original source bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    const SafeXmlDocument document =
        parseSafeXml(
            *source.originalBytes,
            limitOverrides
        );

    ImporterOutput output;
    output.entries.push_back(ImportedEntry{
        .id = source.id + QStringLiteral(":document"),
        .kind = ImportedEntryKind::Document,
        .order = source.order,
        .quantity = 1,
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
        .metadata = QJsonObject{
            {
                QStringLiteral("parser"),
                QStringLiteral("generic-xml")
            },
            {
                QStringLiteral("rootName"),
                document.root.name
            },
            {
                QStringLiteral("rootAttributes"),
                document.root.attributes
            },
        },
    });
    return output;
}

} // namespace tcgprint::imports
