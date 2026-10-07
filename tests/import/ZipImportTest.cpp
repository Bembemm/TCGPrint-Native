#include <QtTest>

#include "import/ZipImport.h"

#include <QCryptographicHash>

#include <zlib.h>

using namespace tcgprint::imports;

namespace {

struct SyntheticEntry {
    QString name;
    QByteArray bytes;
    bool deflate{false};
    bool symlink{false};
};

void append16(
    QByteArray& output,
    std::uint16_t value
)
{
    output.append(
        static_cast<char>(value & 0xffU)
    );
    output.append(
        static_cast<char>((value >> 8) & 0xffU)
    );
}

void append32(
    QByteArray& output,
    std::uint32_t value
)
{
    output.append(
        static_cast<char>(value & 0xffU)
    );
    output.append(
        static_cast<char>((value >> 8) & 0xffU)
    );
    output.append(
        static_cast<char>((value >> 16) & 0xffU)
    );
    output.append(
        static_cast<char>((value >> 24) & 0xffU)
    );
}

QByteArray rawDeflate(const QByteArray& input)
{
    z_stream stream{};
    if (
        deflateInit2(
            &stream,
            Z_BEST_COMPRESSION,
            Z_DEFLATED,
            -MAX_WBITS,
            8,
            Z_DEFAULT_STRATEGY
        ) != Z_OK
    ) {
        return {};
    }

    QByteArray output;
    output.resize(
        static_cast<qsizetype>(
            compressBound(
                static_cast<uLong>(
                    input.size()
                )
            )
        )
    );

    stream.next_in =
        reinterpret_cast<Bytef*>(
            const_cast<char*>(input.constData())
        );
    stream.avail_in =
        static_cast<uInt>(input.size());
    stream.next_out =
        reinterpret_cast<Bytef*>(output.data());
    stream.avail_out =
        static_cast<uInt>(output.size());

    const int result =
        deflate(&stream, Z_FINISH);
    const uLong size =
        stream.total_out;
    deflateEnd(&stream);

    if (result != Z_STREAM_END) {
        return {};
    }

    output.resize(
        static_cast<qsizetype>(size)
    );
    return output;
}

QByteArray makeZip(
    const std::vector<SyntheticEntry>& entries
)
{
    struct BuiltEntry {
        QByteArray name;
        QByteArray compressed;
        std::uint32_t crc{0};
        std::uint32_t uncompressedSize{0};
        std::uint32_t localOffset{0};
        std::uint16_t method{0};
        bool symlink{false};
    };

    QByteArray archive;
    std::vector<BuiltEntry> built;
    built.reserve(entries.size());

    for (const SyntheticEntry& entry : entries) {
        const QByteArray name =
            entry.name.toUtf8();
        const QByteArray compressed =
            entry.deflate
            ? rawDeflate(entry.bytes)
            : entry.bytes;

        if (entry.deflate && compressed.isEmpty()) {
            return {};
        }

        uLong checksum =
            crc32(0L, Z_NULL, 0);
        checksum = crc32(
            checksum,
            reinterpret_cast<const Bytef*>(
                entry.bytes.constData()
            ),
            static_cast<uInt>(
                entry.bytes.size()
            )
        );

        const std::uint32_t localOffset =
            static_cast<std::uint32_t>(
                archive.size()
            );
        const std::uint16_t method =
            entry.deflate ? 8U : 0U;
        const std::uint16_t flags = 0x0800U;

        append32(archive, 0x04034b50U);
        append16(archive, 20U);
        append16(archive, flags);
        append16(archive, method);
        append16(archive, 0U);
        append16(archive, 0U);
        append32(
            archive,
            static_cast<std::uint32_t>(
                checksum
            )
        );
        append32(
            archive,
            static_cast<std::uint32_t>(
                compressed.size()
            )
        );
        append32(
            archive,
            static_cast<std::uint32_t>(
                entry.bytes.size()
            )
        );
        append16(
            archive,
            static_cast<std::uint16_t>(
                name.size()
            )
        );
        append16(archive, 0U);
        archive.append(name);
        archive.append(compressed);

        built.push_back(BuiltEntry{
            .name = name,
            .compressed = compressed,
            .crc =
                static_cast<std::uint32_t>(
                    checksum
                ),
            .uncompressedSize =
                static_cast<std::uint32_t>(
                    entry.bytes.size()
                ),
            .localOffset = localOffset,
            .method = method,
            .symlink = entry.symlink,
        });
    }

    const std::uint32_t centralOffset =
        static_cast<std::uint32_t>(
            archive.size()
        );

    for (const BuiltEntry& entry : built) {
        append32(archive, 0x02014b50U);
        append16(
            archive,
            static_cast<std::uint16_t>(
                (3U << 8) | 20U
            )
        );
        append16(archive, 20U);
        append16(archive, 0x0800U);
        append16(archive, entry.method);
        append16(archive, 0U);
        append16(archive, 0U);
        append32(archive, entry.crc);
        append32(
            archive,
            static_cast<std::uint32_t>(
                entry.compressed.size()
            )
        );
        append32(
            archive,
            entry.uncompressedSize
        );
        append16(
            archive,
            static_cast<std::uint16_t>(
                entry.name.size()
            )
        );
        append16(archive, 0U);
        append16(archive, 0U);
        append16(archive, 0U);
        append16(archive, 0U);
        const std::uint32_t mode =
            entry.symlink
            ? 0120777U
            : 0100644U;
        append32(
            archive,
            mode << 16
        );
        append32(
            archive,
            entry.localOffset
        );
        archive.append(entry.name);
    }

    const std::uint32_t centralSize =
        static_cast<std::uint32_t>(
            archive.size()
        ) - centralOffset;

    append32(archive, 0x06054b50U);
    append16(archive, 0U);
    append16(archive, 0U);
    append16(
        archive,
        static_cast<std::uint16_t>(
            built.size()
        )
    );
    append16(
        archive,
        static_cast<std::uint16_t>(
            built.size()
        )
    );
    append32(archive, centralSize);
    append32(archive, centralOffset);
    append16(archive, 0U);

    return archive;
}

ImportSource zipSource(
    const QByteArray& bytes,
    const QString& filename =
        QStringLiteral("batch.zip")
)
{
    return ImportSource{
        .id = filename,
        .kind = ImportSourceKind::File,
        .filename = filename,
        .order = 0,
        .sizeBytes =
            static_cast<std::uint64_t>(
                bytes.size()
            ),
        .originalBytes = bytes,
    };
}

bool hasError(
    const ZipExpansion& result,
    const QString& code
)
{
    return std::any_of(
        result.errors.begin(),
        result.errors.end(),
        [&code](const ImportWarning& error) {
            return error.code == code;
        }
    );
}

} // namespace

class ZipImportTest final : public QObject
{
    Q_OBJECT

private slots:
    void expandsStoredDeflatedAndNestedEntries()
    {
        const QByteArray nested = makeZip({
            SyntheticEntry{
                .name = QStringLiteral("inside.txt"),
                .bytes = QByteArrayLiteral("2 Island"),
                .deflate = true,
            },
        });
        QVERIFY(!nested.isEmpty());

        const QByteArray archive = makeZip({
            SyntheticEntry{
                .name = QStringLiteral("deck.txt"),
                .bytes = QByteArrayLiteral("1 Sol Ring"),
                .deflate = true,
            },
            SyntheticEntry{
                .name = QStringLiteral("nested/cards.zip"),
                .bytes = nested,
            },
        });
        QVERIFY(!archive.isEmpty());

        const ZipExpansion result =
            expandZipSource(
                zipSource(archive)
            );

        QCOMPARE(result.errors.size(), std::size_t{0});
        QCOMPARE(result.sources.size(), std::size_t{3});
        QCOMPARE(
            result.sources[0].filename.value_or(QString()),
            QStringLiteral("deck.txt")
        );
        QCOMPARE(
            result.sources[0]
                .originalBytes.value_or(QByteArray()),
            QByteArrayLiteral("1 Sol Ring")
        );
        QCOMPARE(
            result.sources[1].filename.value_or(QString()),
            QStringLiteral("cards.zip")
        );
        QCOMPARE(
            result.sources[2].sourcePath.value_or(QString()),
            QStringLiteral(
                "nested/cards.zip!/inside.txt"
            )
        );
        QCOMPARE(
            result.sources[2]
                .originalBytes.value_or(QByteArray()),
            QByteArrayLiteral("2 Island")
        );
        QCOMPARE(
            result.sources[0].sha256.value_or(QString()).size(),
            64
        );
    }

    void blocksUnsafeAndSymlinkEntriesIndividually()
    {
        const QByteArray archive = makeZip({
            {
                .name = QStringLiteral("../escape.txt"),
                .bytes = QByteArrayLiteral("Bad"),
            },
            {
                .name = QStringLiteral("/absolute.txt"),
                .bytes = QByteArrayLiteral("Bad"),
            },
            {
                .name = QStringLiteral("C:/drive.txt"),
                .bytes = QByteArrayLiteral("Bad"),
            },
            {
                .name = QStringLiteral("link.txt"),
                .bytes = QByteArrayLiteral("target"),
                .symlink = true,
            },
            {
                .name = QStringLiteral("safe.txt"),
                .bytes = QByteArrayLiteral("1 Sol Ring"),
            },
        });

        const ZipExpansion result =
            expandZipSource(
                zipSource(archive)
            );

        QCOMPARE(result.sources.size(), std::size_t{1});
        QCOMPARE(
            result.sources.front()
                .filename.value_or(QString()),
            QStringLiteral("safe.txt")
        );
        QVERIFY(
            hasError(
                result,
                QStringLiteral("ZIP_UNSAFE_PATH")
            )
        );
        QVERIFY(
            hasError(
                result,
                QStringLiteral("ZIP_SYMLINK_BLOCKED")
            )
        );
        QVERIFY(result.errors.size() >= std::size_t{4});
    }

    void enforcesZipSafetyLimits()
    {
        const QByteArray twoFiles = makeZip({
            {
                .name = QStringLiteral("a.txt"),
                .bytes = QByteArrayLiteral("1 A"),
            },
            {
                .name = QStringLiteral("b.txt"),
                .bytes = QByteArrayLiteral("1 B"),
            },
        });

        ZipExpansionOptions countOptions;
        countOptions.limits.maxZipEntries = 1;
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(twoFiles),
                    countOptions
                ),
                QStringLiteral("ZIP_ENTRY_LIMIT")
            )
        );

        ZipExpansionOptions entrySizeOptions;
        entrySizeOptions.limits.maxZipEntryBytes = 8;
        const QByteArray oversized = makeZip({
            {
                .name = QStringLiteral("large.txt"),
                .bytes = QByteArrayLiteral("123456789"),
            },
        });
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(oversized),
                    entrySizeOptions
                ),
                QStringLiteral("ZIP_SIZE_LIMIT")
            )
        );

        ZipExpansionOptions aggregateOptions;
        aggregateOptions.limits
            .maxZipTotalUncompressedBytes = 6;
        const QByteArray aggregate = makeZip({
            {
                .name = QStringLiteral("a.txt"),
                .bytes = QByteArrayLiteral("1234"),
            },
            {
                .name = QStringLiteral("b.txt"),
                .bytes = QByteArrayLiteral("5678"),
            },
        });
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(aggregate),
                    aggregateOptions
                ),
                QStringLiteral("ZIP_SIZE_LIMIT")
            )
        );

        const QByteArray repeated(
            5000,
            'a'
        );
        const QByteArray compressed = makeZip({
            {
                .name = QStringLiteral("repeat.txt"),
                .bytes = repeated,
                .deflate = true,
            },
        });
        ZipExpansionOptions ratioOptions;
        ratioOptions.limits
            .maxZipCompressionRatio = 2;
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(compressed),
                    ratioOptions
                ),
                QStringLiteral("ZIP_RATIO_LIMIT")
            )
        );

        ZipExpansionOptions archiveOptions;
        archiveOptions.limits.maxZipArchiveBytes =
            static_cast<std::uint64_t>(
                twoFiles.size() - 1
            );
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(twoFiles),
                    archiveOptions
                ),
                QStringLiteral("ZIP_SIZE_LIMIT")
            )
        );

        const QByteArray inner = makeZip({
            {
                .name = QStringLiteral("leaf.txt"),
                .bytes = QByteArrayLiteral("Island"),
            },
        });
        const QByteArray middle = makeZip({
            {
                .name = QStringLiteral("inner.zip"),
                .bytes = inner,
            },
        });
        const QByteArray outer = makeZip({
            {
                .name = QStringLiteral("middle.zip"),
                .bytes = middle,
            },
        });

        ZipExpansionOptions depthOptions;
        depthOptions.limits.maxZipNestingDepth = 1;
        QVERIFY(
            hasError(
                expandZipSource(
                    zipSource(outer),
                    depthOptions
                ),
                QStringLiteral("ZIP_DEPTH_LIMIT")
            )
        );
    }

    void rejectsInvalidArchivesWithoutFilesystemExtraction()
    {
        const ZipExpansion result =
            expandZipSource(
                zipSource(
                    QByteArrayLiteral(
                        "PK\x03\x04broken"
                    )
                )
            );

        QVERIFY(result.sources.empty());
        QVERIFY(
            hasError(
                result,
                QStringLiteral("ZIP_INVALID")
            )
        );
    }
};

QTEST_APPLESS_MAIN(ZipImportTest)

#include "ZipImportTest.moc"
