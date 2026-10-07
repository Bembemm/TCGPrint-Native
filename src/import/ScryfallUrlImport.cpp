#include "import/ScryfallUrlImport.h"

#include "import/ImportFailure.h"
#include "import/UrlRegistry.h"
#include "import/UrlTransportPolicy.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <stdexcept>

namespace tcgprint::imports {
namespace {

struct ScryfallCardPath {
    QString setCode;
    QString collectorNumber;
    std::optional<QString> slug;
};

bool matches(
    const QString& value,
    const QString& pattern
)
{
    return QRegularExpression(
        pattern,
        QRegularExpression::CaseInsensitiveOption
    ).match(value).hasMatch();
}

ScryfallCardPath parseCardPath(const QUrl& url)
{
    const QStringList encodedParts =
        url.path(QUrl::FullyEncoded)
            .split(
                QLatin1Char('/'),
                Qt::SkipEmptyParts
            );

    if (
        encodedParts.size() != 3
        && encodedParts.size() != 4
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Scryfall URL must be a card URL with an explicit set code and collector number."
            ),
            QStringLiteral("URL_UNSUPPORTED")
        );
    }

    const QString root =
        QUrl::fromPercentEncoding(
            encodedParts[0].toUtf8()
        );
    const QString setCode =
        QUrl::fromPercentEncoding(
            encodedParts[1].toUtf8()
        );
    const QString collector =
        QUrl::fromPercentEncoding(
            encodedParts[2].toUtf8()
        );

    if (
        root != QStringLiteral("card")
        || !matches(
            setCode,
            QStringLiteral(R"(^[a-z0-9]{1,8}$)")
        )
        || !matches(
            collector,
            QStringLiteral(R"(^[a-z0-9*]+$)")
        )
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Scryfall URL must be a card URL with an explicit set code and collector number."
            ),
            QStringLiteral("URL_UNSUPPORTED")
        );
    }

    std::optional<QString> slug;
    if (encodedParts.size() == 4) {
        const QString decoded =
            QUrl::fromPercentEncoding(
                encodedParts[3].toUtf8()
            );
        if (!matches(
                decoded,
                QStringLiteral(
                    R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)"
                )
            )) {
            throw ImportFailureError(
                QStringLiteral(
                    "Scryfall URL contains an invalid card slug."
                ),
                QStringLiteral("URL_UNSUPPORTED")
            );
        }
        slug = decoded;
    }

    return ScryfallCardPath{
        .setCode = setCode.toLower(),
        .collectorNumber = collector,
        .slug = std::move(slug),
    };
}

std::optional<QString> nameSuggestion(
    const std::optional<QString>& slug
)
{
    if (!slug.has_value() || slug->isEmpty()) {
        return std::nullopt;
    }

    static const QSet<QString> stopWords{
        QStringLiteral("a"),
        QStringLiteral("an"),
        QStringLiteral("and"),
        QStringLiteral("for"),
        QStringLiteral("in"),
        QStringLiteral("of"),
        QStringLiteral("the"),
        QStringLiteral("to"),
    };

    QStringList words =
        slug->split(
            QLatin1Char('-'),
            Qt::SkipEmptyParts
        );

    for (qsizetype index = 0; index < words.size(); ++index) {
        QString word = words[index].toLower();
        if (
            index > 0
            && stopWords.contains(word)
        ) {
            words[index] = word;
            continue;
        }

        if (!word.isEmpty()) {
            word[0] = word[0].toUpper();
        }
        words[index] = word;
    }

    const QString joined =
        words.join(QLatin1Char(' '));
    return joined.isEmpty()
        ? std::nullopt
        : std::optional<QString>(joined);
}

} // namespace

ScryfallUrlImportResult importScryfallCardUrl(
    const QString& value,
    const QString& sourceId,
    int order
)
{
    if (sourceId.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "Scryfall URL import requires a non-empty source id"
        );
    }

    const UrlAdapterResolution resolution =
        resolveUrlAdapter(value);
    if (
        resolution.kind != UrlResolutionKind::Adapter
        || resolution.adapterId
            != QStringLiteral("scryfall")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Scryfall URL must be a card URL with an explicit set code and collector number."
            ),
            QStringLiteral("URL_UNSUPPORTED"),
            sourceId
        );
    }

    const QUrl url(
        value.trimmed(),
        QUrl::StrictMode
    );
    ScryfallCardPath card = parseCardPath(url);

    ImportSource source{
        .id = sourceId,
        .kind = ImportSourceKind::Url,
        .filename = QStringLiteral("scryfall-import"),
        .order = order,
        .originalFormat = QStringLiteral("scryfall"),
        .sourceUrl =
            sanitizeUrlForReport(
                url.toString(QUrl::FullyEncoded)
            ),
        .adapterId = QStringLiteral("scryfall"),
        .sizeBytes = 0,
        .metadata = QJsonObject{
            {
                QStringLiteral("adapterId"),
                QStringLiteral("scryfall")
            },
            {
                QStringLiteral("referenceType"),
                QStringLiteral("card-url")
            },
        },
    };

    ImportedEntry entry{
        .id = sourceId
            + QStringLiteral(":scryfall-card"),
        .kind = ImportedEntryKind::DeckCard,
        .order = 0,
        .quantity = 1,
        .sourceId = sourceId,
        .cardHint = ImportedCardHint{
            .setCode = card.setCode,
            .collectorNumber =
                card.collectorNumber,
        },
        .nameSuggestion =
            nameSuggestion(card.slug),
        .metadata = QJsonObject{
            {
                QStringLiteral("adapterId"),
                QStringLiteral("scryfall")
            },
            {
                QStringLiteral("source"),
                QStringLiteral("card-url")
            },
        },
    };

    ImporterOutput output;
    output.entries.push_back(std::move(entry));
    output.metadata = QJsonObject{
        {
            QStringLiteral("adapterId"),
            QStringLiteral("scryfall")
        },
        {
            QStringLiteral("referenceType"),
            QStringLiteral("card-url")
        },
    };

    return ScryfallUrlImportResult{
        .source = std::move(source),
        .output = std::move(output),
    };
}

} // namespace tcgprint::imports
