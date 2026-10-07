#include "import/DirectFileUrlImport.h"

#include "import/ImportDetection.h"
#include "import/ImportFailure.h"
#include "import/UrlTransportPolicy.h"

#include <QCryptographicHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <optional>

namespace tcgprint::imports {
namespace {

const QSet<QString>& supportedMediaTypes()
{
    static const QSet<QString> values{
        QStringLiteral("application/json"),
        QStringLiteral("application/octet-stream"),
        QStringLiteral("application/csv"),
        QStringLiteral("application/xml"),
        QStringLiteral("application/zip"),
        QStringLiteral("application/x-zip-compressed"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/png"),
        QStringLiteral("image/svg+xml"),
        QStringLiteral("image/tiff"),
        QStringLiteral("image/webp"),
        QStringLiteral("text/csv"),
        QStringLiteral("text/plain"),
        QStringLiteral("text/tab-separated-values"),
        QStringLiteral("text/xml"),
    };
    return values;
}

std::optional<ImportKind> expectedKind(
    const QString& mediaType
)
{
    if (mediaType == QStringLiteral("application/json")) {
        return ImportKind::Json;
    }
    if (
        mediaType == QStringLiteral("application/xml")
        || mediaType == QStringLiteral("text/xml")
    ) {
        return ImportKind::GenericXml;
    }
    if (
        mediaType == QStringLiteral("application/zip")
        || mediaType
            == QStringLiteral("application/x-zip-compressed")
    ) {
        return ImportKind::Zip;
    }
    if (mediaType == QStringLiteral("image/svg+xml")) {
        return ImportKind::Svg;
    }
    if (mediaType.startsWith(QStringLiteral("image/"))) {
        return ImportKind::Image;
    }
    if (
        mediaType == QStringLiteral("text/csv")
        || mediaType == QStringLiteral("application/csv")
    ) {
        return ImportKind::Csv;
    }
    if (
        mediaType
        == QStringLiteral("text/tab-separated-values")
    ) {
        return ImportKind::Tsv;
    }
    return std::nullopt;
}

bool textPlainCompatible(ImportKind kind)
{
    return kind == ImportKind::SimpleDecklist
        || kind == ImportKind::ArenaLike
        || kind == ImportKind::MtgoLike
        || kind == ImportKind::XmageLike
        || kind == ImportKind::MwdeckLike
        || kind == ImportKind::Csv
        || kind == ImportKind::Tsv
        || kind == ImportKind::Json
        || kind == ImportKind::GenericXml
        || kind == ImportKind::MpcAutofillXml;
}

bool mimeMatchesCandidate(
    const QString& mediaType,
    const ImportCandidate& candidate
)
{
    const auto expected = expectedKind(mediaType);

    if (!expected.has_value()) {
        if (mediaType == QStringLiteral("text/plain")) {
            return textPlainCompatible(candidate.kind);
        }
        return mediaType
            == QStringLiteral("application/octet-stream");
    }

    if (*expected == ImportKind::GenericXml) {
        return candidate.kind == ImportKind::GenericXml
            || candidate.kind == ImportKind::MpcAutofillXml
            || candidate.kind == ImportKind::Svg;
    }

    if (*expected == ImportKind::Image) {
        if (candidate.kind != ImportKind::Image) {
            return false;
        }

        if (!candidate.originalFormat.has_value()) {
            return true;
        }

        QString format =
            mediaType.mid(QStringLiteral("image/").size());
        if (format == QStringLiteral("jpg")) {
            format = QStringLiteral("jpeg");
        }
        return *candidate.originalFormat == format;
    }

    return candidate.kind == *expected;
}

QString filenameFromUrl(const QUrl& url)
{
    const QStringList segments =
        url.path().split(
            QLatin1Char('/'),
            Qt::SkipEmptyParts
        );
    if (segments.isEmpty()) {
        return {};
    }
    return QUrl::fromPercentEncoding(
        segments.back().toUtf8()
    );
}

QString dispositionFilename(const QString& header)
{
    if (header.isEmpty()) {
        return {};
    }

    const QRegularExpression extended(
        QStringLiteral(
            R"(filename\*\s*=\s*UTF-8''([^;]+))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto extendedMatch = extended.match(header);
    if (extendedMatch.hasMatch()) {
        QString encoded =
            extendedMatch.captured(1).trimmed();
        if (
            encoded.startsWith(QLatin1Char('"'))
            && encoded.endsWith(QLatin1Char('"'))
            && encoded.size() >= 2
        ) {
            encoded = encoded.mid(
                1,
                encoded.size() - 2
            );
        }
        return QUrl::fromPercentEncoding(
            encoded.toUtf8()
        );
    }

    const QRegularExpression regular(
        QStringLiteral(
            R"(filename\s*=\s*(?:"([^"]*)"|([^;\s]*)))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto regularMatch = regular.match(header);
    if (!regularMatch.hasMatch()) {
        return {};
    }

    return !regularMatch.captured(1).isNull()
        ? regularMatch.captured(1)
        : regularMatch.captured(2);
}

QString safeFilename(QString candidate)
{
    candidate.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const qsizetype slash =
        candidate.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        candidate = candidate.mid(slash + 1);
    }

    candidate.remove(
        QRegularExpression(
            QStringLiteral(R"([\x00-\x1f\x7f])")
        )
    );
    candidate = candidate.trimmed().left(180);

    if (
        candidate.isEmpty()
        || candidate == QStringLiteral(".")
        || candidate == QStringLiteral("..")
    ) {
        return QStringLiteral("download");
    }
    return candidate;
}

bool looksLikeHtml(const QByteArray& bytes)
{
    QString prefix = QString::fromUtf8(
        bytes.constData(),
        std::min<qsizetype>(
            bytes.size(),
            static_cast<qsizetype>(8192)
        )
    );
    if (
        !prefix.isEmpty()
        && prefix.front() == QChar(0xfeff)
    ) {
        prefix.remove(0, 1);
    }
    prefix = prefix.trimmed();

    const QRegularExpression html(
        QStringLiteral(
            R"(^(?:<!doctype\s+html\b|<html(?:\s|>)))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    return html.match(prefix).hasMatch();
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

QString sourceExtension(const QString& filename)
{
    const qsizetype dot =
        filename.lastIndexOf(QLatin1Char('.'));
    if (
        dot < 0
        || dot == filename.size() - 1
    ) {
        return {};
    }
    return filename.mid(dot + 1).toLower();
}

const ImportCandidate* usableCandidate(
    const ImportDetection& detection
)
{
    if (detection.selected.has_value()) {
        return &*detection.selected;
    }

    const auto found = std::find_if(
        detection.candidates.begin(),
        detection.candidates.end(),
        [](const ImportCandidate& candidate) {
            return candidate.kind != ImportKind::Unknown;
        }
    );
    return found == detection.candidates.end()
        ? nullptr
        : &*found;
}

} // namespace

DirectFileUrlImportResult importDirectFileUrl(
    const QString& value,
    const QString& sourceId,
    int order,
    const UrlFetchOptions& fetchOptions
)
{
    if (sourceId.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "Direct URL import requires a non-empty source id"
        );
    }

    UrlPayload payload =
        fetchUrlPayload(value, fetchOptions);

    if (
        payload.mediaType == QStringLiteral("text/html")
        || payload.mediaType
            == QStringLiteral("application/xhtml+xml")
        || looksLikeHtml(payload.bytes)
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "The URL returned an HTML page. HTML scraping is not supported; provide a direct import file URL."
            ),
            QStringLiteral("URL_CONTENT_TYPE"),
            sourceId
        );
    }

    if (
        !supportedMediaTypes().contains(
            payload.mediaType
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "The URL returned an unsupported Content-Type."
            ),
            QStringLiteral("URL_CONTENT_TYPE"),
            sourceId
        );
    }

    const QString disposition =
        payload.headers.value(
            QStringLiteral("content-disposition")
        );
    const QString filename =
        safeFilename(
            !dispositionFilename(disposition).isEmpty()
            ? dispositionFilename(disposition)
            : filenameFromUrl(payload.finalUrl)
        );

    ImportDetection detection =
        detectImport(
            ImportDetectionInput{
                .bytes = payload.bytes,
                .fileName = filename,
                .mediaType = payload.mediaType,
            }
        );

    const ImportCandidate* candidate =
        usableCandidate(detection);
    if (candidate == nullptr) {
        throw ImportFailureError(
            QStringLiteral(
                "The URL response does not contain a supported image, deck text, CSV, JSON, XML, or ZIP file."
            ),
            QStringLiteral("URL_CONTENT_TYPE"),
            sourceId
        );
    }

    const bool mismatch =
        !mimeMatchesCandidate(
            payload.mediaType,
            *candidate
        );
    const QString sha = hashBytes(payload.bytes);

    QString originalFormat;
    if (candidate->originalFormat.has_value()) {
        originalFormat = *candidate->originalFormat;
    } else {
        originalFormat = sourceExtension(filename);
        if (originalFormat.isEmpty()) {
            originalFormat =
                importKindName(candidate->kind);
        }
    }

    const QString reportedFinalUrl =
        sanitizeUrlForReport(
            payload.finalUrl.toString(
                QUrl::FullyEncoded
            )
        );
    const QString reportedRequestedUrl =
        sanitizeUrlForReport(value);

    ImportSource source{
        .id = sourceId,
        .kind = ImportSourceKind::Url,
        .filename = filename,
        .order = order,
        .originalFormat = originalFormat,
        .mediaType = payload.mediaType,
        .sourceUrl = reportedFinalUrl,
        .sizeBytes =
            static_cast<std::uint64_t>(
                payload.bytes.size()
            ),
        .originalBytes = payload.bytes,
        .sha256 = sha,
        .metadata = QJsonObject{
            {
                QStringLiteral("requestedUrl"),
                reportedRequestedUrl
            },
            {
                QStringLiteral("responseMediaType"),
                payload.mediaType
            },
            {
                QStringLiteral("responseBytes"),
                static_cast<qint64>(
                    payload.bytes.size()
                )
            },
            {
                QStringLiteral("sha256"),
                sha
            },
        },
    };

    std::vector<ImportWarning> warnings;
    if (mismatch) {
        source.metadata.insert(
            QStringLiteral("contentTypeMismatch"),
            true
        );
        warnings.push_back(ImportWarning{
            .code = QStringLiteral(
                "URL_CONTENT_TYPE_MISMATCH"
            ),
            .message = QStringLiteral(
                "URL Content-Type does not match the detected content; bytes remain authoritative."
            ),
            .sourceId = sourceId,
            .sourceFilename = filename,
        });
    }

    return DirectFileUrlImportResult{
        .source = std::move(source),
        .detection = std::move(detection),
        .warnings = std::move(warnings),
    };
}

} // namespace tcgprint::imports
