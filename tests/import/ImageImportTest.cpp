#include <QtTest>

#include "import/ImageImport.h"
#include "import/ImportFailure.h"

#include <QBuffer>
#include <QImage>

using namespace tcgprint::imports;

namespace {

ImportSource source(
    const QByteArray& bytes,
    const QString& filename
)
{
    return ImportSource{
        .id = QStringLiteral("input-1"),
        .kind = ImportSourceKind::File,
        .filename = filename,
        .order = 0,
        .sizeBytes =
            static_cast<std::uint64_t>(bytes.size()),
        .originalBytes = bytes,
    };
}

QByteArray rasterBytes(
    const char* format,
    int width,
    int height
)
{
    QImage image(
        width,
        height,
        QImage::Format_ARGB32
    );
    image.fill(qRgba(22, 118, 204, 255));

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    if (!image.save(&buffer, format)) {
        return {};
    }
    return bytes;
}

} // namespace

class ImageImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsPngAndJpegWithoutChangingOriginalBytes()
    {
        const struct {
            const char* writerFormat;
            QString filename;
            QString expectedFormat;
            QString expectedMediaType;
            int width;
            int height;
        } cases[] = {
            {
                "PNG",
                QStringLiteral("raster.png"),
                QStringLiteral("png"),
                QStringLiteral("image/png"),
                4,
                3,
            },
            {
                "JPEG",
                QStringLiteral("raster.jpeg"),
                QStringLiteral("jpeg"),
                QStringLiteral("image/jpeg"),
                8,
                6,
            },
        };

        for (const auto& item : cases) {
            const QByteArray bytes =
                rasterBytes(
                    item.writerFormat,
                    item.width,
                    item.height
                );
            QVERIFY2(
                !bytes.isEmpty(),
                item.writerFormat
            );
            const ImporterOutput result =
                importImageSource(
                    source(bytes, item.filename)
                );

            QCOMPARE(result.entries.size(), std::size_t{1});
            QVERIFY(result.warnings.empty());
            const ImportedEntry& entry =
                result.entries.front();
            QCOMPARE(
                static_cast<int>(entry.kind),
                static_cast<int>(
                    ImportedEntryKind::CustomCard
                )
            );
            QVERIFY(entry.asset.has_value());
            QCOMPARE(
                entry.asset->originalFormat,
                item.expectedFormat
            );
            QCOMPARE(
                entry.asset->mediaType.value_or(QString()),
                item.expectedMediaType
            );
            QCOMPARE(
                entry.asset->widthPx.value_or(0.0),
                static_cast<double>(item.width)
            );
            QCOMPARE(
                entry.asset->heightPx.value_or(0.0),
                static_cast<double>(item.height)
            );
            QCOMPARE(
                entry.asset->originalBytes.value_or(QByteArray()),
                bytes
            );
            QCOMPARE(
                entry.nameSuggestion.value_or(QString()),
                QStringLiteral("raster")
            );
            QCOMPARE(
                entry.asset->sha256.value_or(QString()).size(),
                64
            );
        }
    }

    void contentWinsMisleadingExtension()
    {
        const QByteArray jpeg =
            rasterBytes("JPEG", 5, 7);
        QVERIFY(!jpeg.isEmpty());
        const ImporterOutput result =
            importImageSource(
                source(
                    jpeg,
                    QStringLiteral("scan.png")
                )
            );

        QCOMPARE(
            result.entries.front()
                .asset->originalFormat,
            QStringLiteral("jpeg")
        );
        QCOMPARE(result.warnings.size(), std::size_t{1});
        QCOMPARE(
            result.warnings.front().code,
            QStringLiteral("EXTENSION_MISMATCH")
        );
    }

    void preservesSvgBytesAndDimensions()
    {
        const QByteArray svg = QByteArrayLiteral(
            "<?xml version=\"1.0\"?>"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" "
            "width=\"120\" height=\"168\" "
            "viewBox=\"0 0 40 56\">"
            "<rect width=\"40\" height=\"56\" />"
            "</svg>"
        );

        const ImporterOutput result =
            importImageSource(
                source(
                    svg,
                    QStringLiteral("card-front.svg")
                )
            );

        const ImportedAsset& asset =
            *result.entries.front().asset;
        QCOMPARE(
            asset.originalFormat,
            QStringLiteral("svg")
        );
        QCOMPARE(
            asset.mediaType.value_or(QString()),
            QStringLiteral("image/svg+xml")
        );
        QCOMPARE(asset.widthPx.value_or(0.0), 120.0);
        QCOMPARE(asset.heightPx.value_or(0.0), 168.0);
        QCOMPARE(
            asset.originalBytes.value_or(QByteArray()),
            svg
        );
        QCOMPARE(
            asset.metadata
                .value(QStringLiteral("viewBox"))
                .toString(),
            QStringLiteral("0 0 40 56")
        );
    }

    void rejectsTruncatedRasterAndWrongSvgRoot()
    {
        const QByteArray corruptPng =
            QByteArray::fromHex(
                "89504e470d0a1a0a00000000"
            );
        try {
            static_cast<void>(
                importImageSource(
                    source(
                        corruptPng,
                        QStringLiteral("broken.png")
                    )
                )
            );
            QFAIL("Expected raster decode failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("IMAGE_DECODE_FAILED")
            );
        }

        try {
            static_cast<void>(
                importImageSource(
                    source(
                        QByteArrayLiteral(
                            "<deck><card /></deck>"
                        ),
                        QStringLiteral("fake.svg")
                    )
                )
            );
            QFAIL("Expected unsupported image.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("UNSUPPORTED_INPUT")
            );
        }
    }

    void enforcesByteAndPixelLimits()
    {
        const QByteArray png =
            rasterBytes("PNG", 4, 3);
        QVERIFY(!png.isEmpty());

        ImportLimitOverrides bytesLimit;
        bytesLimit.maxInputBytes =
            static_cast<std::uint64_t>(
                png.size() - 1
            );
        try {
            static_cast<void>(
                importImageSource(
                    source(
                        png,
                        QStringLiteral("large.png")
                    ),
                    bytesLimit
                )
            );
            QFAIL("Expected byte limit failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INPUT_TOO_LARGE")
            );
        }

        ImportLimitOverrides pixelLimit;
        pixelLimit.maxRasterPixels = 11;
        try {
            static_cast<void>(
                importImageSource(
                    source(
                        png,
                        QStringLiteral("pixels.png")
                    ),
                    pixelLimit
                )
            );
            QFAIL("Expected pixel limit failure.");
        } catch (const ImportFailureError& error) {
            QCOMPARE(
                error.code(),
                QStringLiteral("INPUT_TOO_LARGE")
            );
        }
    }
};

QTEST_APPLESS_MAIN(ImageImportTest)

#include "ImageImportTest.moc"
