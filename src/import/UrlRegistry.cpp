#include "import/UrlRegistry.h"

#include "import/ImportFailure.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <array>

namespace tcgprint::imports {
namespace {

struct KnownSite {
    const char* id;
    const char* domain;
    const char* message;
};

constexpr std::array<KnownSite, 9> KnownSites{{
    {"scryfall", "scryfall.com", "Este caminho do Scryfall ainda nao tem adapter de importacao."},
    {"moxfield", "moxfield.com", "Moxfield nao esta disponivel para importacao automatizada neste momento."},
    {"archidekt", "archidekt.com", "Este caminho do Archidekt ainda nao tem adapter de importacao."},
    {"cubecobra", "cubecobra.com", "Este caminho do CubeCobra ainda nao tem adapter de importacao."},
    {"deckstats", "deckstats.net", "Deckstats nao esta disponivel para importacao automatizada neste momento."},
    {"mtggoldfish", "mtggoldfish.com", "MTGGoldfish nao esta disponivel para importacao automatizada neste momento."},
    {"mtgtop8", "mtgtop8.com", "Este caminho do MTGTop8 ainda nao tem adapter de importacao."},
    {"tappedout", "tappedout.net", "TappedOut nao esta disponivel para importacao automatizada neste momento."},
    {"mtg-wtf", "mtg.wtf", "Este caminho do mtg.wtf ainda nao tem adapter de importacao."},
}};

bool exactHost(const QString& host, const QString& domain)
{
    return host == domain || host == QStringLiteral("www.") + domain;
}

bool knownSiteHost(const QString& host, const QString& domain)
{
    return host == domain
        || host.endsWith(QStringLiteral(".") + domain);
}

bool regexMatches(
    const QString& value,
    const QString& pattern,
    QRegularExpression::PatternOptions options =
        QRegularExpression::NoPatternOption
)
{
    return QRegularExpression(pattern, options)
        .match(value)
        .hasMatch();
}

bool matchesScryfall(const QUrl& url)
{
    if (url.scheme() != QStringLiteral("https")) {
        return false;
    }

    const QStringList parts =
        url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (
        parts.size() != 3
        && parts.size() != 4
    ) {
        return false;
    }
    if (parts[0] != QStringLiteral("card")) {
        return false;
    }

    const QString setCode =
        QUrl::fromPercentEncoding(parts[1].toUtf8());
    const QString collector =
        QUrl::fromPercentEncoding(parts[2].toUtf8());
    if (
        !regexMatches(
            setCode,
            QStringLiteral(R"(^[a-z0-9]{1,8}$)"),
            QRegularExpression::CaseInsensitiveOption
        )
        || !regexMatches(
            collector,
            QStringLiteral(R"(^[a-z0-9*]+$)"),
            QRegularExpression::CaseInsensitiveOption
        )
    ) {
        return false;
    }

    if (parts.size() == 4) {
        const QString slug =
            QUrl::fromPercentEncoding(parts[3].toUtf8());
        if (!regexMatches(
                slug,
                QStringLiteral(
                    R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)"
                ),
                QRegularExpression::CaseInsensitiveOption
            )) {
            return false;
        }
    }

    return true;
}

bool matchesArchidekt(const QUrl& url)
{
    return url.scheme() == QStringLiteral("https")
        && regexMatches(
            url.path(),
            QStringLiteral(R"(^/decks/\d{1,12}/?$)")
        );
}

bool matchesCubeCobra(const QUrl& url)
{
    return url.scheme() == QStringLiteral("https")
        && regexMatches(
            url.path(),
            QStringLiteral(
                R"(^/cube/overview/[a-z0-9_-]{2,80}/?$)"
            ),
            QRegularExpression::CaseInsensitiveOption
        );
}

bool matchesMtgWtf(const QUrl& url)
{
    return url.scheme() == QStringLiteral("https")
        && regexMatches(
            url.path(),
            QStringLiteral(
                R"(^/deck/[a-z0-9]{2,8}/[a-z0-9]+(?:-[a-z0-9]+)*(?:/download)?/?$)"
            ),
            QRegularExpression::CaseInsensitiveOption
        );
}

bool validDeckId(const QString& value)
{
    return regexMatches(
        value,
        QStringLiteral(R"(^\d{1,12}$)")
    );
}

bool validFormatName(const QString& value)
{
    return regexMatches(
        value,
        QStringLiteral(R"(^[a-z0-9_-]{1,100}$)"),
        QRegularExpression::CaseInsensitiveOption
    );
}

bool matchesMtgTop8(const QUrl& url)
{
    if (url.scheme() != QStringLiteral("https")) {
        return false;
    }

    const QUrlQuery query(url);
    const QString id =
        query.queryItemValue(QStringLiteral("d"));
    if (!validDeckId(id)) {
        return false;
    }

    if (url.path() == QStringLiteral("/event")) {
        return true;
    }

    return url.path() == QStringLiteral("/dec")
        && validFormatName(
            query.queryItemValue(QStringLiteral("f"))
        );
}

bool matchesAdapter(
    const QString& id,
    const QUrl& url
)
{
    if (id == QStringLiteral("scryfall")) {
        return matchesScryfall(url);
    }
    if (id == QStringLiteral("archidekt")) {
        return matchesArchidekt(url);
    }
    if (id == QStringLiteral("cubecobra")) {
        return matchesCubeCobra(url);
    }
    if (id == QStringLiteral("mtg-wtf")) {
        return matchesMtgWtf(url);
    }
    if (id == QStringLiteral("mtgtop8")) {
        return matchesMtgTop8(url);
    }
    return false;
}

QString adapterForHost(const QString& host)
{
    for (const QString& id : {
        QStringLiteral("scryfall"),
        QStringLiteral("archidekt"),
        QStringLiteral("cubecobra"),
        QStringLiteral("mtgtop8"),
        QStringLiteral("mtg-wtf"),
    }) {
        QString domain = id;
        if (id == QStringLiteral("cubecobra")) {
            domain = QStringLiteral("cubecobra.com");
        } else if (id == QStringLiteral("archidekt")) {
            domain = QStringLiteral("archidekt.com");
        } else if (id == QStringLiteral("scryfall")) {
            domain = QStringLiteral("scryfall.com");
        } else if (id == QStringLiteral("mtgtop8")) {
            domain = QStringLiteral("mtgtop8.com");
        } else if (id == QStringLiteral("mtg-wtf")) {
            domain = QStringLiteral("mtg.wtf");
        }

        if (exactHost(host, domain)) {
            return id;
        }
    }
    return {};
}

const KnownSite* knownSiteForHost(const QString& host)
{
    for (const KnownSite& site : KnownSites) {
        if (knownSiteHost(
                host,
                QString::fromLatin1(site.domain)
            )) {
            return &site;
        }
    }
    return nullptr;
}

QUrl parseHttpUrl(const QString& value)
{
    const QRegularExpression schemePattern(
        QStringLiteral(R"(^\s*([a-z][a-z\d+.-]*):)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto schemeMatch =
        schemePattern.match(value);
    if (!schemeMatch.hasMatch()) {
        throw ImportFailureError(
            QStringLiteral("URL input has no valid scheme."),
            QStringLiteral("URL_INVALID")
        );
    }

    const QString scheme =
        schemeMatch.captured(1).toLower();
    if (
        scheme != QStringLiteral("http")
        && scheme != QStringLiteral("https")
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "Somente URLs HTTP e HTTPS podem ser importadas."
            ),
            QStringLiteral("URL_UNSUPPORTED")
        );
    }

    if (!regexMatches(
            value,
            QStringLiteral(R"(^\s*https?://)"),
            QRegularExpression::CaseInsensitiveOption
        )) {
        throw ImportFailureError(
            QStringLiteral(
                "Importable URLs must begin with http:// or https://."
            ),
            QStringLiteral("URL_INVALID")
        );
    }

    const QString trimmed = value.trimmed();
    const QUrl url(trimmed, QUrl::StrictMode);
    if (
        !url.isValid()
        || url.host().isEmpty()
        || !url.userName().isEmpty()
        || !url.password().isEmpty()
    ) {
        throw ImportFailureError(
            QStringLiteral(
                "URL invalida: informe um host publico sem credenciais embutidas."
            ),
            QStringLiteral("URL_INVALID")
        );
    }

    return url;
}

} // namespace

UrlAdapterResolution resolveUrlAdapter(const QString& value)
{
    const QUrl url = parseHttpUrl(value);
    const QString host = url.host().toLower();

    const QString adapterId =
        adapterForHost(host);
    if (
        !adapterId.isEmpty()
        && matchesAdapter(adapterId, url)
    ) {
        return UrlAdapterResolution{
            .kind = UrlResolutionKind::Adapter,
            .adapterId = adapterId,
        };
    }

    if (!adapterId.isEmpty()) {
        return UrlAdapterResolution{
            .kind = UrlResolutionKind::KnownUnsupported,
            .siteId = adapterId,
            .message =
                QStringLiteral(
                    "Este caminho ainda nao e suportado pelo adapter "
                )
                + adapterId
                + QStringLiteral("."),
        };
    }

    if (
        const KnownSite* site =
            knownSiteForHost(host)
    ) {
        return UrlAdapterResolution{
            .kind = UrlResolutionKind::KnownUnsupported,
            .siteId = QString::fromLatin1(site->id),
            .message = QString::fromUtf8(site->message),
        };
    }

    return UrlAdapterResolution{
        .kind = UrlResolutionKind::DirectFile,
    };
}

} // namespace tcgprint::imports
