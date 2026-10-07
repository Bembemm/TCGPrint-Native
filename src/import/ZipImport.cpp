#include "import/ZipImport.h"

#include "import/ImportDetection.h"
#include "import/SourcePath.h"

#include <QCryptographicHash>
#include <QStringDecoder>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include <zlib.h>

namespace tcgprint::imports {
namespace {

constexpr std::uint32_t LocalHeaderSignature = 0x04034b50U;
constexpr std::uint32_t CentralHeaderSignature = 0x02014b50U;
constexpr std::uint32_t EndSignature = 0x06054b50U;

struct ZipEntryMetadata {
    QString path;
    QByteArray rawName;
    bool directory{false};
    bool symlink{false};
    std::uint16_t flags{0};
    std::uint16_t method{0};
    std::uint32_t crc{0};
    std::uint32_t compressedSize{0};
    std::uint32_t uncompressedSize{0};
    std::uint32_t localHeaderOffset{0};
};

struct ExpansionState {
    ImportLimits limits;
    bool expandNestedArchives{true};
    std::vector<ImportWarning> errors;
    int nextOrder{0};
    std::uint64_t entriesSeen{0};
    std::uint64_t uncompressedBytes{0};
    bool stopEntries{false};
};

bool hasRange(
    const QByteArray& bytes,
    qsizetype offset,
    qsizetype length
)
{
    return offset >= 0
        && length >= 0
        && offset <= bytes.size()
        && length <= bytes.size() - offset;
}

std::uint16_t read16(
    const QByteArray& bytes,
    qsizetype offset
)
{
    if (!hasRange(bytes, offset, 2)) {
        throw std::out_of_range("ZIP 16-bit read out of range");
    }
    const auto* data =
        reinterpret_cast<const unsigned char*>(
            bytes.constData() + offset
        );
    return static_cast<std::uint16_t>(
        data[0]
        | (static_cast<std::uint16_t>(data[1]) << 8)
    );
}

std::uint32_t read32(
    const QByteArray& bytes,
    qsizetype offset
)
{
    if (!hasRange(bytes, offset, 4)) {
        throw std::out_of_range("ZIP 32-bit read out of range");
    }
    const auto* data =
        reinterpret_cast<const unsigned char*>(
            bytes.constData() + offset
        );
    return static_cast<std::uint32_t>(
        data[0]
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24)
    );
}

ImportWarning zipError(
    const ImportSource& source,
    QString code,
    QString message,
    std::optional<QString> path = std::nullopt
)
{
    return ImportWarning{
        .code = std::move(code),
        .message = std::move(message),
        .sourceId = source.id,
        .sourceFilename = source.filename,
        .sourcePath =
            path.has_value()
            ? path
            : source.sourcePath,
    };
}

std::optional<qsizetype> findEndRecord(
    const QByteArray& bytes
)
{
    if (bytes.size() < 22) {
        return std::nullopt;
    }

    const qsizetype minimum =
        std::max<qsizetype>(
            0,
            bytes.size() - (22 + 65535)
        );

    for (
        qsizetype offset = bytes.size() - 22;
        offset >= minimum;
        --offset
    ) {
        if (read32(bytes, offset) != EndSignature) {
            continue;
        }

        const std::uint16_t commentLength =
            read16(bytes, offset + 20);
        if (
            hasRange(
                bytes,
                offset,
                static_cast<qsizetype>(22 + commentLength)
            )
            && offset + 22 + commentLength == bytes.size()
        ) {
            return offset;
        }
    }
    return std::nullopt;
}

std::optional<QString> decodeFileName(
    const QByteArray& raw,
    bool utf8
)
{
    if (raw.isEmpty() || raw.contains('\0')) {
        return std::nullopt;
    }

    if (!utf8) {
        return QString::fromLatin1(raw);
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString value = decoder.decode(raw);
    if (decoder.hasError()) {
        return std::nullopt;
    }
    return value;
}

std::optional<QString> safeZipPath(
    const QString& rawPath
)
{
    try {
        return sanitizeRelativeImportPath(rawPath);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<ZipEntryMetadata> parseCentralDirectory(
    const ImportSource& source,
    const QByteArray& bytes,
    ExpansionState& state
)
{
    const auto endOffset = findEndRecord(bytes);
    if (!endOffset.has_value()) {
        throw std::runtime_error("ZIP end record not found");
    }

    const std::uint16_t diskNumber =
        read16(bytes, *endOffset + 4);
    const std::uint16_t centralDisk =
        read16(bytes, *endOffset + 6);
    const std::uint16_t entriesOnDisk =
        read16(bytes, *endOffset + 8);
    const std::uint16_t totalEntries =
        read16(bytes, *endOffset + 10);
    const std::uint32_t centralSize =
        read32(bytes, *endOffset + 12);
    const std::uint32_t centralOffset =
        read32(bytes, *endOffset + 16);

    if (
        diskNumber != 0
        || centralDisk != 0
        || entriesOnDisk != totalEntries
        || totalEntries == 0xffffU
        || centralSize == 0xffffffffU
        || centralOffset == 0xffffffffU
    ) {
        throw std::runtime_error(
            "Multi-disk or ZIP64 archives are not supported"
        );
    }

    if (
        !hasRange(
            bytes,
            static_cast<qsizetype>(centralOffset),
            static_cast<qsizetype>(centralSize)
        )
        || static_cast<std::uint64_t>(centralOffset)
            + centralSize
            > static_cast<std::uint64_t>(*endOffset)
    ) {
        throw std::runtime_error(
            "ZIP central directory is outside the archive"
        );
    }

    std::vector<ZipEntryMetadata> entries;
    entries.reserve(totalEntries);
    qsizetype cursor =
        static_cast<qsizetype>(centralOffset);

    for (
        std::uint16_t index = 0;
        index < totalEntries;
        ++index
    ) {
        ++state.entriesSeen;
        if (state.entriesSeen > state.limits.maxZipEntries) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_ENTRY_LIMIT"),
                    QStringLiteral(
                        "ZIP tree exceeds the configured entry limit."
                    )
                )
            );
            state.stopEntries = true;
            break;
        }

        if (
            !hasRange(bytes, cursor, 46)
            || read32(bytes, cursor)
                != CentralHeaderSignature
        ) {
            throw std::runtime_error(
                "ZIP central directory entry is invalid"
            );
        }

        const std::uint16_t versionMadeBy =
            read16(bytes, cursor + 4);
        const std::uint16_t flags =
            read16(bytes, cursor + 8);
        const std::uint16_t method =
            read16(bytes, cursor + 10);
        const std::uint32_t crc =
            read32(bytes, cursor + 16);
        const std::uint32_t compressedSize =
            read32(bytes, cursor + 20);
        const std::uint32_t uncompressedSize =
            read32(bytes, cursor + 24);
        const std::uint16_t nameLength =
            read16(bytes, cursor + 28);
        const std::uint16_t extraLength =
            read16(bytes, cursor + 30);
        const std::uint16_t commentLength =
            read16(bytes, cursor + 32);
        const std::uint32_t externalAttributes =
            read32(bytes, cursor + 38);
        const std::uint32_t localHeaderOffset =
            read32(bytes, cursor + 42);

        const qsizetype recordLength =
            46
            + static_cast<qsizetype>(nameLength)
            + static_cast<qsizetype>(extraLength)
            + static_cast<qsizetype>(commentLength);
        if (!hasRange(bytes, cursor, recordLength)) {
            throw std::runtime_error(
                "ZIP central directory entry is truncated"
            );
        }

        const QByteArray rawName =
            bytes.mid(cursor + 46, nameLength);
        const auto decoded =
            decodeFileName(
                rawName,
                (flags & 0x0800U) != 0
            );

        const std::uint32_t unixMode =
            externalAttributes >> 16;
        const bool symlink =
            (unixMode & 0xf000U) == 0xa000U;
        const bool unixDirectory =
            ((versionMadeBy >> 8) & 0xffU) == 3U
            && (unixMode & 0xf000U) == 0x4000U;

        if (!decoded.has_value()) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_UNSAFE_PATH"),
                    QStringLiteral(
                        "Blocked ZIP entry with an invalid filename."
                    )
                )
            );
            cursor += recordLength;
            continue;
        }

        const bool slashDirectory =
            decoded->endsWith(QLatin1Char('/'))
            || decoded->endsWith(QLatin1Char('\\'));

        const auto normalized =
            safeZipPath(*decoded);
        if (!normalized.has_value()) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_UNSAFE_PATH"),
                    QStringLiteral(
                        "Blocked ZIP entry with an unsafe relative path."
                    ),
                    *decoded
                )
            );
            cursor += recordLength;
            continue;
        }

        if (symlink) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_SYMLINK_BLOCKED"),
                    QStringLiteral(
                        "Blocked symlink ZIP entry."
                    ),
                    *normalized
                )
            );
            cursor += recordLength;
            continue;
        }

        entries.push_back(ZipEntryMetadata{
            .path = *normalized,
            .rawName = rawName,
            .directory =
                slashDirectory || unixDirectory,
            .symlink = false,
            .flags = flags,
            .method = method,
            .crc = crc,
            .compressedSize = compressedSize,
            .uncompressedSize = uncompressedSize,
            .localHeaderOffset = localHeaderOffset,
        });

        cursor += recordLength;
    }

    return entries;
}

QByteArray inflateRaw(
    const QByteArray& input,
    std::uint32_t expectedSize
)
{
    QByteArray output;
    output.resize(
        expectedSize == 0
        ? 1
        : static_cast<qsizetype>(expectedSize)
    );

    z_stream stream{};
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

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw std::runtime_error(
            "Could not initialize ZIP inflater"
        );
    }

    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (
        result != Z_STREAM_END
        || stream.total_out != expectedSize
    ) {
        throw std::runtime_error(
            "ZIP deflate stream is invalid"
        );
    }

    output.resize(
        static_cast<qsizetype>(expectedSize)
    );
    return output;
}

QByteArray readEntryBytes(
    const QByteArray& archive,
    const ZipEntryMetadata& entry
)
{
    const qsizetype localOffset =
        static_cast<qsizetype>(entry.localHeaderOffset);
    if (
        !hasRange(archive, localOffset, 30)
        || read32(archive, localOffset)
            != LocalHeaderSignature
    ) {
        throw std::runtime_error(
            "ZIP local header is invalid"
        );
    }

    const std::uint16_t localFlags =
        read16(archive, localOffset + 6);
    const std::uint16_t localMethod =
        read16(archive, localOffset + 8);
    const std::uint16_t nameLength =
        read16(archive, localOffset + 26);
    const std::uint16_t extraLength =
        read16(archive, localOffset + 28);

    if ((localFlags & 0x0001U) != 0) {
        throw std::runtime_error(
            "Encrypted ZIP entries are unsupported"
        );
    }
    if (localMethod != entry.method) {
        throw std::runtime_error(
            "ZIP compression method disagrees between headers"
        );
    }

    const qsizetype dataOffset =
        localOffset
        + 30
        + static_cast<qsizetype>(nameLength)
        + static_cast<qsizetype>(extraLength);

    if (
        !hasRange(
            archive,
            dataOffset,
            static_cast<qsizetype>(entry.compressedSize)
        )
    ) {
        throw std::runtime_error(
            "ZIP entry data is truncated"
        );
    }

    const QByteArray compressed =
        archive.mid(
            dataOffset,
            static_cast<qsizetype>(entry.compressedSize)
        );

    QByteArray output;
    if (entry.method == 0) {
        if (
            entry.compressedSize
            != entry.uncompressedSize
        ) {
            throw std::runtime_error(
                "Stored ZIP entry size is inconsistent"
            );
        }
        output = compressed;
    } else if (entry.method == 8) {
        output =
            inflateRaw(
                compressed,
                entry.uncompressedSize
            );
    } else {
        throw std::runtime_error(
            "Unsupported ZIP compression method"
        );
    }

    uLong checksum = crc32(0L, Z_NULL, 0);
    checksum = crc32(
        checksum,
        reinterpret_cast<const Bytef*>(
            output.constData()
        ),
        static_cast<uInt>(output.size())
    );
    if (
        static_cast<std::uint32_t>(checksum)
        != entry.crc
    ) {
        throw std::runtime_error(
            "ZIP entry CRC does not match"
        );
    }

    return output;
}

QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256
        ).toHex()
    );
}

void expandRecursive(
    const ImportSource& source,
    ExpansionState& state,
    std::uint64_t depth,
    std::vector<ImportSource>& expanded
)
{
    if (!source.originalBytes.has_value()) {
        state.errors.push_back(
            zipError(
                source,
                QStringLiteral("ZIP_INVALID"),
                QStringLiteral(
                    "ZIP source has no original bytes."
                )
            )
        );
        return;
    }

    const QByteArray& bytes =
        *source.originalBytes;
    if (
        static_cast<std::uint64_t>(bytes.size())
        > state.limits.maxZipArchiveBytes
    ) {
        state.errors.push_back(
            zipError(
                source,
                QStringLiteral("ZIP_SIZE_LIMIT"),
                QStringLiteral(
                    "ZIP archive exceeds the configured archive byte limit."
                )
            )
        );
        return;
    }

    std::vector<ZipEntryMetadata> entries;
    try {
        entries =
            parseCentralDirectory(
                source,
                bytes,
                state
            );
    } catch (const std::exception&) {
        state.errors.push_back(
            zipError(
                source,
                QStringLiteral("ZIP_INVALID"),
                QStringLiteral(
                    "ZIP archive has an invalid central directory."
                )
            )
        );
        return;
    }

    for (const ZipEntryMetadata& entry : entries) {
        if (state.stopEntries) {
            break;
        }
        if (entry.directory) {
            continue;
        }

        if (
            entry.uncompressedSize
                > state.limits.maxZipEntryBytes
            || state.uncompressedBytes
                + entry.uncompressedSize
                > state.limits.maxZipTotalUncompressedBytes
        ) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_SIZE_LIMIT"),
                    QStringLiteral(
                        "ZIP entry exceeds the configured individual or aggregate uncompressed size limit."
                    ),
                    entry.path
                )
            );
            continue;
        }

        const double ratio =
            entry.uncompressedSize == 0
            ? 0.0
            : (
                entry.compressedSize == 0
                ? std::numeric_limits<double>::infinity()
                : static_cast<double>(
                    entry.uncompressedSize
                ) / static_cast<double>(
                    entry.compressedSize
                )
            );
        if (
            ratio
            > static_cast<double>(
                state.limits.maxZipCompressionRatio
            )
        ) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_RATIO_LIMIT"),
                    QStringLiteral(
                        "ZIP entry exceeds the configured compression ratio."
                    ),
                    entry.path
                )
            );
            continue;
        }

        QByteArray childBytes;
        try {
            childBytes =
                readEntryBytes(bytes, entry);
        } catch (const std::exception&) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_INVALID"),
                    QStringLiteral(
                        "ZIP entry data is invalid."
                    ),
                    entry.path
                )
            );
            continue;
        }

        if (
            static_cast<std::uint64_t>(
                childBytes.size()
            ) > state.limits.maxZipEntryBytes
            || state.uncompressedBytes
                + static_cast<std::uint64_t>(
                    childBytes.size()
                )
                > state.limits.maxZipTotalUncompressedBytes
        ) {
            state.errors.push_back(
                zipError(
                    source,
                    QStringLiteral("ZIP_SIZE_LIMIT"),
                    QStringLiteral(
                        "ZIP entry expanded beyond the configured safety bound."
                    ),
                    entry.path
                )
            );
            continue;
        }

        state.uncompressedBytes +=
            static_cast<std::uint64_t>(
                childBytes.size()
            );

        const QString childPath =
            source.sourcePath.has_value()
            ? *source.sourcePath
                + QStringLiteral("!/")
                + entry.path
            : entry.path;

        QString filename = entry.path;
        const qsizetype slash =
            filename.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            filename = filename.mid(slash + 1);
        }

        ImportSource child{
            .id = source.id
                + QStringLiteral("!/")
                + entry.path
                + QStringLiteral("#")
                + QString::number(state.nextOrder),
            .kind = ImportSourceKind::ZipEntry,
            .filename = filename,
            .sourcePath = childPath,
            .parentSourceId = source.id,
            .order = state.nextOrder++,
            .sizeBytes =
                static_cast<std::uint64_t>(
                    childBytes.size()
                ),
            .originalBytes = childBytes,
            .sha256 = sha256(childBytes),
        };

        expanded.push_back(child);

        const ImportDetection detection =
            detectImport(ImportDetectionInput{
                .bytes = childBytes,
                .fileName = filename,
            });

        if (
            state.expandNestedArchives
            && detection.selected.has_value()
            && detection.selected->kind
                == ImportKind::Zip
        ) {
            if (
                depth
                >= state.limits.maxZipNestingDepth
            ) {
                state.errors.push_back(
                    zipError(
                        child,
                        QStringLiteral("ZIP_DEPTH_LIMIT"),
                        QStringLiteral(
                            "Nested ZIP exceeds the configured depth limit."
                        ),
                        childPath
                    )
                );
            } else {
                expandRecursive(
                    child,
                    state,
                    depth + 1,
                    expanded
                );
            }
        }
    }
}

} // namespace

ZipExpansion expandZipSource(
    const ImportSource& source,
    const ZipExpansionOptions& options
)
{
    ExpansionState state{
        .limits =
            resolveImportLimits(options.limits),
        .expandNestedArchives =
            options.expandNestedArchives,
        .nextOrder = source.order + 1,
    };

    std::vector<ImportSource> sources;
    expandRecursive(
        source,
        state,
        0,
        sources
    );

    return ZipExpansion{
        .sources = std::move(sources),
        .errors = std::move(state.errors),
        .entriesSeen = state.entriesSeen,
        .uncompressedBytes =
            state.uncompressedBytes,
    };
}

} // namespace tcgprint::imports
