#include "import/MtgTop8UrlImport.h"

#include "import/ImportFailure.h"
#include "import/TextImport.h"
#include "import/UrlRegistry.h"
#include "import/UrlTransportPolicy.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace tcgprint::imports {
namespace {

inline constexpr std::uint64_t MaxEventPageBytes =
    5ULL * 1024ULL * 1024ULL;

QString hashBytes(const QByteArray& bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            bytes,
            QCryptographicHash::Sha256
        ).toHex()
    );
}

bool validDeckId(const QString& value)
{
    return QRegularExpression(
        QStringLiteral(R"(^\d{1,12}$)")
    ).match(value).hasMatch();
}

bool validFormatName(const QString& value)
{
    return QRegularExpression(
        QStringLiteral(R"(^[a-z0-9_-]{1,100}$)"),
        QRegularExpression::CaseInsensitiveOption
    ).match(value).hasMatch();
}

QString deckIdFromUrl(
    const QString& value,
    const QString& sourceId
)
{
    const UrlAdapterResolution resolution =
        resolveUrlAdapter(value);
    if (
        resolution.kind != UrlResolutionKind::Adapter
        || resolution.adapterId
            != QStringLiteral("mtgtop8")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 URL must be an event page or explicit .mwDeck export URL."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }

    const QUrl url(value.trimmed(), QUrl::StrictMode);
    const QString deckId =
        QUrlQuery(url).queryItemValue(
            QStringLiteral("d")
        );
    if (!validDeckId(deckId)) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 URL must contain a valid deck id."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }
    return deckId;
}

QString mediaCharset(const UrlPayload& payload)
{
    const QString raw =
        payload.headers.value(
            QStringLiteral("content-type")
        );
    const QRegularExpression charsetPattern(
        QStringLiteral(
            R"(charset\s*=\s*(?:"([^"]+)"|'([^']+)'|([^;\s]+)))"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match =
        charsetPattern.match(raw);
    if (!match.hasMatch()) {
        return QStringLiteral("utf-8");
    }

    for (int index = 1; index <= 3; ++index) {
        const QString candidate =
            match.captured(index).trimmed();
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    return QStringLiteral("utf-8");
}

QString decodeTextPayload(
    const UrlPayload& payload,
    const QString& sourceId
)
{
    const QByteArray charset =
        mediaCharset(payload)
            .toLatin1()
            .trimmed();

    QStringDecoder decoder(charset.constData());
    if (!decoder.isValid()) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 response declared an unsupported text charset."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const QString text =
        decoder.decode(payload.bytes);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 response could not be decoded using its declared charset."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }
    return text;
}

QString decodeHtmlAttribute(QString value)
{
    value.replace(
        QRegularExpression(
            QStringLiteral(R"(&amp;)"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QStringLiteral("&")
    );
    value.replace(
        QRegularExpression(
            QStringLiteral(R"(&quot;)"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QStringLiteral(""")
    );
    value.replace(
        QRegularExpression(
            QStringLiteral(R"(&(?:#39|apos);)"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QStringLiteral("'")
    );

    const QRegularExpression numeric(
        QStringLiteral(
            R"(&#(?:x([0-9a-f]+)|([0-9]+));)"
        ),
        QRegularExpression::CaseInsensitiveOption
    );

    qsizetype offset = 0;
    while (true) {
        const auto match =
            numeric.match(value, offset);
        if (!match.hasMatch()) {
            break;
        }

        bool ok = false;
        uint codePoint = 0;
        if (!match.captured(1).isEmpty()) {
            codePoint = match.captured(1)
                .toUInt(&ok, 16);
        } else {
            codePoint = match.captured(2)
                .toUInt(&ok, 10);
        }

        QString replacement;
        if (
            ok
            && codePoint <= 0x10ffffU
            && !(codePoint >= 0xd800U
                && codePoint <= 0xdfffU)
        ) {
            replacement =
                QString::fromUcs4(&codePoint, 1);
        } else {
            replacement = match.captured(0);
        }

        value.replace(
            match.capturedStart(),
            match.capturedLength(),
            replacement
        );
        offset =
            match.capturedStart()
            + replacement.size();
    }

    return value;
}

bool mtgTop8Host(const QString& host)
{
    const QString lower = host.toLower();
    return lower == QStringLiteral("mtgtop8.com")
        || lower == QStringLiteral("www.mtgtop8.com");
}

std::optional<QUrl> exportUrlFromPage(
    const QString& html,
    const QUrl& pageUrl,
    const QString& expectedId
)
{
    const QRegularExpression anchorPattern(
        QStringLiteral(R"(<a\b[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpression hrefPattern(
        QStringLiteral(
            R"REGEX(\bhref\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))REGEX"
        ),
        QRegularExpression::CaseInsensitiveOption
    );

    QList<QUrl> candidates;
    QSet<QString> seen;

    auto iterator =
        anchorPattern.globalMatch(html);
    while (iterator.hasNext()) {
        const QString anchor =
            iterator.next().captured(0);
        const auto hrefMatch =
            hrefPattern.match(anchor);
        if (!hrefMatch.hasMatch()) {
            continue;
        }

        QString raw;
        for (int index = 1; index <= 3; ++index) {
            if (!hrefMatch.captured(index).isEmpty()) {
                raw = hrefMatch.captured(index);
                break;
            }
        }
        if (raw.isEmpty()) {
            continue;
        }

        const QUrl relative(
            decodeHtmlAttribute(raw),
            QUrl::StrictMode
        );
        const QUrl target =
            pageUrl.resolved(relative);
        if (
            !target.isValid()
            || target.scheme() != QStringLiteral("https")
            || !mtgTop8Host(target.host())
            || target.path() != QStringLiteral("/dec")
        ) {
            continue;
        }

        const QUrlQuery query(target);
        if (
            query.queryItemValue(QStringLiteral("d"))
                != expectedId
            || !validFormatName(
                query.queryItemValue(
                    QStringLiteral("f")
                )
            )
        ) {
            continue;
        }

        const QString key =
            target.toString(QUrl::FullyEncoded);
        if (!seen.contains(key)) {
            seen.insert(key);
            candidates.push_back(target);
        }
    }

    if (candidates.size() != 1) {
        return std::nullopt;
    }
    return candidates.front();
}

QString safeFilename(
    const QString& contentDisposition,
    const QString& fallback
)
{
    const QRegularExpression filenamePattern(
        QStringLiteral(
            R"REGEX(filename\s*=\s*(?:"([^"]*)"|'([^']*)'|([^;\s]+)))REGEX"
        ),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match =
        filenamePattern.match(contentDisposition);

    QString candidate;
    if (match.hasMatch()) {
        for (int index = 1; index <= 3; ++index) {
            if (!match.captured(index).isEmpty()) {
                candidate = match.captured(index);
                break;
            }
        }
    }

    if (candidate.isEmpty()) {
        candidate = fallback;
    }

    candidate.replace(
        QLatin1Char('\\'),
        QLatin1Char('/')
    );
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
    return candidate.isEmpty()
        ? fallback
        : candidate;
}

bool recognizableMwDeck(const QString& text)
{
    if (
        QRegularExpression(
            QStringLiteral(R"(^\s*<)")
        ).match(text).hasMatch()
    ) {
        return false;
    }

    return QRegularExpression(
        QStringLiteral(
            R"((?:Deck file created with mtgtop8\.com|Deck file for Magic Workstation|Magic Workstation|^\s*\d+\s+\[[a-z0-9]{2,8}\]))"
        ),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::MultilineOption
    ).match(text).hasMatch();
}

std::uint64_t remainingMs(
    const QElapsedTimer& timer,
    std::uint64_t total,
    const QString& sourceId
)
{
    const qint64 elapsed = timer.elapsed();
    if (
        elapsed < 0
        || static_cast<std::uint64_t>(elapsed)
            >= total
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 import exceeded its total request timeout."
            ),
            QStringLiteral("URL_TIMEOUT"),
            sourceId
        );
    }
    return total
        - static_cast<std::uint64_t>(elapsed);
}

UrlFetchOptions boundedOptions(
    const UrlFetchOptions& base,
    const QElapsedTimer& timer,
    std::uint64_t maximumBytes,
    const QString& sourceId
)
{
    UrlFetchOptions options = base;
    options.policy.timeoutMs =
        remainingMs(
            timer,
            base.policy.timeoutMs,
            sourceId
        );
    options.policy.maxResponseBytes =
        maximumBytes;
    return options;
}

} // namespace

MtgTop8UrlImportResult importMtgTop8Url(
    const QString& value,
    const QString& sourceId,
    int order,
    const UrlFetchOptions& fetchOptions
)
{
    if (sourceId.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "MTGTop8 URL import requires a non-empty source id"
        );
    }
    validateUrlTransportPolicy(fetchOptions.policy);

    const QString id =
        deckIdFromUrl(value, sourceId);
    const QUrl originalUrl(
        value.trimmed(),
        QUrl::StrictMode
    );

    QElapsedTimer timer;
    timer.start();

    QUrl exportUrl;
    if (originalUrl.path() == QStringLiteral("/dec")) {
        exportUrl = originalUrl;
    } else {
        const std::uint64_t pageLimit =
            std::min<std::uint64_t>(
                fetchOptions.policy.maxResponseBytes,
                MaxEventPageBytes
            );
        const UrlPayload page =
            fetchUrlPayload(
                originalUrl,
                boundedOptions(
                    fetchOptions,
                    timer,
                    pageLimit,
                    sourceId
                )
            );

        if (page.mediaType != QStringLiteral("text/html")) {
            throw ImportFailureError(
                QStringLiteral(
                    "MTGTop8 event page did not return HTML containing an export link."
                ),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const QUrl finalPageUrl =
            page.finalUrl;
        if (
            !mtgTop8Host(finalPageUrl.host())
            || QUrlQuery(finalPageUrl)
                .queryItemValue(
                    QStringLiteral("d")
                )
                != id
        ) {
            throw ImportFailureError(
                QStringLiteral(
                    "MTGTop8 redirected to an unexpected deck page."
                ),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }

        const QString html =
            decodeTextPayload(page, sourceId);
        const auto found =
            exportUrlFromPage(
                html,
                finalPageUrl,
                id
            );
        if (!found.has_value()) {
            throw ImportFailureError(
                QStringLiteral(
                    "MTGTop8 event page no longer exposes one unambiguous .mwDeck download link for this deck."
                ),
                QStringLiteral("URL_ADAPTER_PAYLOAD"),
                sourceId
            );
        }
        exportUrl = *found;
    }

    const UrlPayload payload =
        fetchUrlPayload(
            exportUrl,
            boundedOptions(
                fetchOptions,
                timer,
                fetchOptions.policy.maxResponseBytes,
                sourceId
            )
        );

    if (payload.mediaType != QStringLiteral("text/plain")) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 export did not return the expected .mwDeck plain-text file."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const QString text =
        decodeTextPayload(payload, sourceId);
    if (!recognizableMwDeck(text)) {
        throw ImportFailureError(
            QStringLiteral(
                "MTGTop8 response is not a recognizable Magic Workstation deck export."
            ),
            QStringLiteral("URL_ADAPTER_PAYLOAD"),
            sourceId
        );
    }

    const QString filename =
        safeFilename(
            payload.headers.value(
                QStringLiteral(
                    "content-disposition"
                )
            ),
            QStringLiteral("mtgtop8-%1.mwDeck")
                .arg(id)
        );

    const QString sha =
        hashBytes(payload.bytes);

    ImportSource source{
        .id = sourceId,
        .kind = ImportSourceKind::Url,
        .filename = filename,
        .order = order,
        .originalFormat = QStringLiteral("mwdeck"),
        .mediaType = QStringLiteral("text/plain"),
        .sourceUrl = sanitizeUrlForReport(
            originalUrl.toString(
                QUrl::FullyEncoded
            )
        ),
        .adapterId = QStringLiteral("mtgtop8"),
        .sizeBytes =
            static_cast<std::uint64_t>(
                payload.bytes.size()
            ),
        .originalBytes = payload.bytes,
        .originalText = text,
        .sha256 = sha,
        .metadata = QJsonObject{
            {
                QStringLiteral("adapterId"),
                QStringLiteral("mtgtop8")
            },
            {
                QStringLiteral("importer"),
                QStringLiteral("mwdeck-like")
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
            {
                QStringLiteral("downloadedUrl"),
                sanitizeUrlForReport(
                    payload.finalUrl.toString(
                        QUrl::FullyEncoded
                    )
                )
            },
            {
                QStringLiteral("sourceFilename"),
                filename
            },
        },
    };

    ImporterOutput output =
        parseTextImport(
            text,
            ImportKind::MwdeckLike,
            source
        );
    output.metadata.insert(
        QStringLiteral("adapterId"),
        QStringLiteral("mtgtop8")
    );
    output.metadata.insert(
        QStringLiteral("importer"),
        QStringLiteral("mwdeck-like")
    );

    return MtgTop8UrlImportResult{
        .source = std::move(source),
        .output = std::move(output),
    };
}

} // namespace tcgprint::imports
