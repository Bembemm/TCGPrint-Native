#include "import/ImageImport.h"

#include "import/ImportDetection.h"
#include "import/ImportFailure.h"
#include "import/XmlImport.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace tcgprint::imports {
namespace {

const QByteArray& inputBytes(const ImportSource& source)
{
    if (
        !source.originalBytes.has_value()
        || source.originalBytes->isEmpty()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Image import requires non-empty original file bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }
    return *source.originalBytes;
}

QString hashBytes(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256
        ).toHex()
    );
}

std::optional<QString> readExtension(
    const std::optional<QString>& filename
)
{
    if (!filename.has_value()) {
        return std::nullopt;
    }

    QString baseName = *filename;
    const qsizetype slash = std::max(
        baseName.lastIndexOf(QLatin1Char('/')),
        baseName.lastIndexOf(QLatin1Char('\\'))
    );
    if (slash >= 0) {
        baseName = baseName.mid(slash + 1);
    }

    const qsizetype dot = baseName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == baseName.size() - 1) {
        return std::nullopt;
    }
    return baseName.mid(dot + 1).toLower();
}

std::optional<QString> nameSuggestion(
    const std::optional<QString>& filename
)
{
    if (!filename.has_value()) {
        return std::nullopt;
    }

    QString baseName = *filename;
    const qsizetype slash = std::max(
        baseName.lastIndexOf(QLatin1Char('/')),
        baseName.lastIndexOf(QLatin1Char('\\'))
    );
    if (slash >= 0) {
        baseName = baseName.mid(slash + 1);
    }
    if (baseName.isEmpty()) {
        return std::nullopt;
    }

    const qsizetype dot = baseName.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) {
        baseName = baseName.left(dot);
    }
    return baseName.isEmpty()
        ? std::nullopt
        : std::optional<QString>(baseName);
}

QString normalizedRasterFormat(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("jpg")) {
        return QStringLiteral("jpeg");
    }
    if (value == QStringLiteral("tif")) {
        return QStringLiteral("tiff");
    }
    return value;
}

std::optional<QString> extensionFormat(const QString& extension)
{
    if (extension == QStringLiteral("png")) {
        return QStringLiteral("png");
    }
    if (
        extension == QStringLiteral("jpg")
        || extension == QStringLiteral("jpeg")
    ) {
        return QStringLiteral("jpeg");
    }
    if (extension == QStringLiteral("webp")) {
        return QStringLiteral("webp");
    }
    if (
        extension == QStringLiteral("tif")
        || extension == QStringLiteral("tiff")
    ) {
        return QStringLiteral("tiff");
    }
    if (extension == QStringLiteral("svg")) {
        return QStringLiteral("svg");
    }
    return std::nullopt;
}

QString mediaTypeFor(const QString& format)
{
    if (format == QStringLiteral("png")) {
        return QStringLiteral("image/png");
    }
    if (format == QStringLiteral("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (format == QStringLiteral("webp")) {
        return QStringLiteral("image/webp");
    }
    if (format == QStringLiteral("tiff")) {
        return QStringLiteral("image/tiff");
    }
    if (format == QStringLiteral("svg")) {
        return QStringLiteral("image/svg+xml");
    }
    return {};
}

std::optional<double> parseSvgLength(
    const QJsonValue& rawValue
)
{
    if (!rawValue.isString()) {
        return std::nullopt;
    }

    const QString text = rawValue.toString();
    const QRegularExpression pattern(
        QStringLiteral(
            R"(^\s*([+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(px|in|cm|mm|pt|pc)?\s*$)"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }

    const QString unit = match.captured(2).toLower();
    const double multiplier =
        unit == QStringLiteral("in") ? 96.0
        : unit == QStringLiteral("cm") ? 96.0 / 2.54
        : unit == QStringLiteral("mm") ? 96.0 / 25.4
        : unit == QStringLiteral("pt") ? 96.0 / 72.0
        : unit == QStringLiteral("pc") ? 16.0
        : 1.0;
    return value * multiplier;
}

std::vector<ImportWarning> extensionWarnings(
    const ImportSource& source,
    const QString& actualFormat
)
{
    const auto extension = readExtension(source.filename);
    if (!extension.has_value()) {
        return {};
    }

    const auto expected = extensionFormat(*extension);
    if (
        !expected.has_value()
        || *expected == actualFormat
    ) {
        return {};
    }

    return std::vector<ImportWarning>{
        ImportWarning{
            .code = QStringLiteral("EXTENSION_MISMATCH"),
            .message = QStringLiteral(
                "File extension does not match image content; bytes are authoritative."
            ),
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
        },
    };
}

ImportedAsset svgAsset(
    const ImportSource& source,
    const QByteArray& bytes,
    const ImportLimits& limits
)
{
    if (
        static_cast<std::uint64_t>(bytes.size())
        > limits.maxSvgBytes
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "SVG exceeds the configured byte limit."
            ),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    SafeXmlDocument document;
    try {
        ImportLimitOverrides xmlLimits;
        xmlLimits.maxXmlBytes = limits.maxSvgBytes;
        xmlLimits.maxXmlDepth = limits.maxXmlDepth;
        xmlLimits.maxXmlNodes = limits.maxXmlNodes;
        document = parseSafeXml(bytes, xmlLimits);
    } catch (const ImportFailureError& error) {
        if (error.code() == QStringLiteral("XML_DTD_BLOCKED")) {
            throw;
        }
        throw ImportFailureError(
            QStringLiteral(
                "SVG is not a safe, well formed XML image."
            ),
            QStringLiteral("INVALID_SVG"),
            source.id,
            source.sourcePath
        );
    }

    if (document.root.name != QStringLiteral("svg")) {
        throw ImportFailureError(
            QStringLiteral(
                "SVG input must have an <svg> document element."
            ),
            QStringLiteral("INVALID_SVG"),
            source.id,
            source.sourcePath
        );
    }

    QJsonObject metadata{
        {QStringLiteral("vector"), true},
    };
    if (
        document.root.attributes.contains(
            QStringLiteral("viewBox")
        )
    ) {
        metadata.insert(
            QStringLiteral("viewBox"),
            document.root.attributes.value(
                QStringLiteral("viewBox")
            )
        );
    } else if (
        document.root.attributes.contains(
            QStringLiteral("viewbox")
        )
    ) {
        metadata.insert(
            QStringLiteral("viewBox"),
            document.root.attributes.value(
                QStringLiteral("viewbox")
            )
        );
    }

    return ImportedAsset{
        .id = source.id + QStringLiteral(":asset"),
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
        .originalFormat = QStringLiteral("svg"),
        .mediaType = QStringLiteral("image/svg+xml"),
        .sha256 = hashBytes(bytes),
        .widthPx = parseSvgLength(
            document.root.attributes.value(
                QStringLiteral("width")
            )
        ),
        .heightPx = parseSvgLength(
            document.root.attributes.value(
                QStringLiteral("height")
            )
        ),
        .originalBytes = bytes,
        .metadata = std::move(metadata),
    };
}

ImportedAsset rasterAsset(
    const ImportSource& source,
    const QByteArray& bytes,
    const QString& expectedFormat,
    const ImportLimits& limits
)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        throw ImportFailureError(
            QStringLiteral("Could not open raster bytes."),
            QStringLiteral("IMAGE_DECODE_FAILED"),
            source.id,
            source.sourcePath
        );
    }

    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(false);

    if (!reader.canRead()) {
        throw ImportFailureError(
            QStringLiteral(
                "Could not safely decode the raster image."
            ),
            QStringLiteral("IMAGE_DECODE_FAILED"),
            source.id,
            source.sourcePath
        );
    }

    const QString actualFormat =
        normalizedRasterFormat(
            QString::fromLatin1(reader.format())
        );
    if (
        actualFormat.isEmpty()
        || actualFormat != expectedFormat
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Raster decoder identified an unexpected format."
            ),
            QStringLiteral("IMAGE_DECODE_FAILED"),
            source.id,
            source.sourcePath
        );
    }

    const QSize advertisedSize = reader.size();
    if (
        advertisedSize.isValid()
        && advertisedSize.width() > 0
        && advertisedSize.height() > 0
    ) {
        const std::uint64_t width =
            static_cast<std::uint64_t>(
                advertisedSize.width()
            );
        const std::uint64_t height =
            static_cast<std::uint64_t>(
                advertisedSize.height()
            );
        if (
            height != 0
            && width > limits.maxRasterPixels / height
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "Raster dimensions exceed the configured pixel limit."
                ),
                QStringLiteral("INPUT_TOO_LARGE"),
                source.id,
                source.sourcePath
            );
        }
    }

    const QImage image = reader.read();
    if (
        image.isNull()
        || image.width() <= 0
        || image.height() <= 0
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Could not safely decode the raster image."
            ),
            QStringLiteral("IMAGE_DECODE_FAILED"),
            source.id,
            source.sourcePath
        );
    }

    const std::uint64_t width =
        static_cast<std::uint64_t>(image.width());
    const std::uint64_t height =
        static_cast<std::uint64_t>(image.height());
    if (
        height != 0
        && width > limits.maxRasterPixels / height
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Raster dimensions exceed the configured pixel limit."
            ),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    return ImportedAsset{
        .id = source.id + QStringLiteral(":asset"),
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
        .originalFormat = expectedFormat,
        .mediaType = mediaTypeFor(expectedFormat),
        .sha256 = hashBytes(bytes),
        .widthPx = static_cast<double>(image.width()),
        .heightPx = static_cast<double>(image.height()),
        .originalBytes = bytes,
        .metadata = QJsonObject{
            {
                QStringLiteral("validatedBy"),
                QStringLiteral("QImageReader-decode")
            },
            {
                QStringLiteral("depth"),
                image.depth()
            },
            {
                QStringLiteral("hasAlpha"),
                image.hasAlphaChannel()
            },
        },
    };
}

} // namespace

ImporterOutput importImageSource(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides
)
{
    const ImportLimits limits =
        resolveImportLimits(limitOverrides);
    const QByteArray& bytes = inputBytes(source);

    if (
        static_cast<std::uint64_t>(bytes.size())
        > limits.maxInputBytes
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Image exceeds the configured input byte limit."
            ),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    const ImportDetection detection =
        detectImport(ImportDetectionInput{
            .bytes = bytes,
            .fileName = source.filename,
        });

    if (
        !detection.selected.has_value()
        || !detection.selected->originalFormat.has_value()
        || (
            detection.selected->kind != ImportKind::Image
            && detection.selected->kind != ImportKind::Svg
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Input bytes do not identify a supported image format."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    const QString format =
        *detection.selected->originalFormat;
    ImportedAsset asset =
        format == QStringLiteral("svg")
        ? svgAsset(source, bytes, limits)
        : rasterAsset(source, bytes, format, limits);

    ImporterOutput output;
    output.warnings =
        extensionWarnings(source, format);

    ImportedEntry entry{
        .id = source.id + QStringLiteral(":entry"),
        .kind = ImportedEntryKind::CustomCard,
        .order = source.order,
        .quantity = 1,
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath = source.sourcePath,
        .nameSuggestion =
            nameSuggestion(source.filename),
        .asset = std::move(asset),
        .metadata = QJsonObject{
            {
                QStringLiteral("importKind"),
                importKindName(
                    detection.selected->kind
                )
            },
            {
                QStringLiteral("originalFormat"),
                format
            },
        },
    };
    output.entries.push_back(std::move(entry));
    return output;
}

} // namespace tcgprint::imports
