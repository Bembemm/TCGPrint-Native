#include "import/SourcePath.h"

#include <QRegularExpression>
#include <QStringList>

#include <stdexcept>

namespace tcgprint::imports {

std::optional<QString> sanitizeRelativeImportPath(
    const std::optional<QString>& value
)
{
    if (!value.has_value() || value->isEmpty()) {
        return std::nullopt;
    }

    if (value->size() > 1024) {
        throw std::length_error(
            "Import file path exceeds 1024 characters."
        );
    }

    for (const QChar character : *value) {
        const ushort code = character.unicode();
        if (code == 0 || (code >= 1 && code <= 0x1f) || code == 0x7f) {
            throw std::invalid_argument(
                "Import file path contains control characters."
            );
        }
    }

    QString normalized = *value;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (
        normalized.startsWith(QLatin1Char('/'))
        || QRegularExpression(
            QStringLiteral(R"(^[a-z]:)"),
            QRegularExpression::CaseInsensitiveOption
        ).match(normalized).hasMatch()
    ) {
        throw std::invalid_argument(
            "Import file path must be relative."
        );
    }

    const QStringList segments =
        normalized.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (segments.contains(QStringLiteral(".."))) {
        throw std::invalid_argument(
            "Import file path cannot contain parent-directory segments."
        );
    }

    QStringList safeSegments;
    safeSegments.reserve(segments.size());
    for (const QString& segment : segments) {
        if (segment.isEmpty() || segment == QStringLiteral(".")) {
            continue;
        }
        safeSegments.push_back(segment);
    }

    const QString path = safeSegments.join(QLatin1Char('/'));
    if (path.size() > 1024) {
        throw std::length_error(
            "Import file path exceeds 1024 characters."
        );
    }

    return path.isEmpty()
        ? std::nullopt
        : std::optional<QString>(path);
}

} // namespace tcgprint::imports
