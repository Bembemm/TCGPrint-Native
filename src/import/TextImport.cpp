#include "import/TextImport.h"

#include "import/ImportFailure.h"

#include <QRegularExpression>

#include <limits>

namespace tcgprint::imports {
namespace {

struct ParsedCardLine {
    std::uint64_t quantity{1};
    QString name;
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
};

std::optional<QString> sectionFor(const QString& line)
{
    QString value = line.trimmed().toLower();
    if (value.endsWith(QLatin1Char(':'))) {
        value.chop(1);
    }

    if (
        value == QStringLiteral("mainboard")
        || value == QStringLiteral("main")
        || value == QStringLiteral("main deck")
        || value == QStringLiteral("deck")
    ) return QStringLiteral("Mainboard");
    if (
        value == QStringLiteral("sideboard")
        || value == QStringLiteral("side")
    ) return QStringLiteral("Sideboard");
    if (value == QStringLiteral("commander")) {
        return QStringLiteral("Commander");
    }
    if (
        value == QStringLiteral("maybeboard")
        || value == QStringLiteral("maybe board")
    ) return QStringLiteral("Maybeboard");
    if (value == QStringLiteral("companion")) {
        return QStringLiteral("Companion");
    }
    return std::nullopt;
}

bool containsForbiddenControls(const QString& value)
{
    for (const QChar character : value) {
        const ushort code = character.unicode();
        if (
            code <= 0x0008
            || code == 0x000b
            || code == 0x000c
            || (code >= 0x000e && code <= 0x001f)
        ) {
            return true;
        }
    }
    return false;
}

std::optional<ParsedCardLine> parseCardLine(
    const QString& raw,
    ImportKind kind
)
{
    QString line = raw.trimmed();
    if (line.isEmpty()) {
        return std::nullopt;
    }

    if (kind == ImportKind::MtgoLike) {
        const QRegularExpression sideboard(
            QStringLiteral(R"(^SB\s*:\s*)"),
            QRegularExpression::CaseInsensitiveOption
        );
        const auto match = sideboard.match(line);
        if (match.hasMatch()) {
            line = line.mid(match.capturedLength()).trimmed();
        }
    }

    std::uint64_t quantity = 1;
    const QRegularExpression quantityPattern(
        QStringLiteral(R"(^(\d+)\s*x?\s+(.+)$)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto quantityMatch = quantityPattern.match(line);
    if (quantityMatch.hasMatch()) {
        bool ok = false;
        quantity = quantityMatch.captured(1).toULongLong(&ok);
        constexpr std::uint64_t MaxSafeInteger =
            9'007'199'254'740'991ULL;
        if (!ok || quantity == 0 || quantity > MaxSafeInteger) {
            return std::nullopt;
        }
        line = quantityMatch.captured(2).trimmed();
    }

    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    const QRegularExpression printing(
        QStringLiteral(
            R"(^(.*?)\s+\(([A-Za-z0-9]{2,8})\)\s+([A-Za-z0-9][A-Za-z0-9/-]*)$)"
        )
    );
    const auto printingMatch = printing.match(line);
    if (
        printingMatch.hasMatch()
        && !printingMatch.captured(1).trimmed().isEmpty()
    ) {
        line = printingMatch.captured(1).trimmed();
        setCode = printingMatch.captured(2).toUpper();
        collectorNumber = printingMatch.captured(3);
    }

    if (kind == ImportKind::MwdeckLike) {
        const QRegularExpression mwsSet(
            QStringLiteral(R"(^\[([A-Za-z0-9]{2,8})\]\s*(.+)$)")
        );
        const auto mwsMatch = mwsSet.match(line);
        if (
            mwsMatch.hasMatch()
            && !mwsMatch.captured(2).trimmed().isEmpty()
        ) {
            setCode = mwsMatch.captured(1).toUpper();
            line = mwsMatch.captured(2).trimmed();
        }
    }

    if (line.isEmpty() || containsForbiddenControls(line)) {
        return std::nullopt;
    }

    return ParsedCardLine{
        .quantity = quantity,
        .name = line,
        .setCode = std::move(setCode),
        .collectorNumber = std::move(collectorNumber),
    };
}

bool matches(
    const QString& text,
    const QString& pattern,
    QRegularExpression::PatternOptions options =
        QRegularExpression::NoPatternOption
)
{
    return QRegularExpression(pattern, options).match(text).hasMatch();
}

bool adapterMatches(
    ImportKind kind,
    const QString& text,
    const std::optional<ImportSource>& source
)
{
    const auto ciMultiline =
        QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::MultilineOption;
    const auto multiline =
        QRegularExpression::MultilineOption;

    switch (kind) {
    case ImportKind::ArenaLike:
        return matches(
                   text,
                   QStringLiteral(R"(^\s*Deck\s*$)"),
                   ciMultiline
               )
            && matches(
                   text,
                   QStringLiteral(
                       R"(^\s*\d+\s+.+\s+\([A-Za-z0-9]{2,8}\)\s+[A-Za-z0-9][A-Za-z0-9/-]*\s*$)"
                   ),
                   multiline
               );
    case ImportKind::MtgoLike:
        return matches(
            text,
            QStringLiteral(R"(^\s*SB\s*:\s*\d+\s+\S)"),
            ciMultiline
        );
    case ImportKind::XmageLike:
        return matches(
            text,
            QStringLiteral(R"(^\s*LAYOUT\s+MAIN\s*$)"),
            ciMultiline
        );
    case ImportKind::MwdeckLike: {
        if (
            matches(
                text,
                QStringLiteral(
                    R"(Deck file for Magic Workstation|Magic Workstation)"
                ),
                QRegularExpression::CaseInsensitiveOption
            )
        ) {
            return true;
        }
        if (
            source.has_value()
            && source->filename.has_value()
            && source->filename->toLower().endsWith(
                QStringLiteral(".mwdeck")
            )
        ) {
            return matches(
                text,
                QStringLiteral(
                    R"(^\s*\d+\s+\[[A-Za-z0-9]{2,8}\])"
                ),
                multiline
            );
        }
        return false;
    }
    case ImportKind::SimpleDecklist:
        return true;
    default:
        return false;
    }
}

std::optional<QString> mapSectionForLine(
    ImportKind kind,
    const QString& raw,
    const std::optional<QString>& section
)
{
    if (
        kind == ImportKind::MtgoLike
        && matches(
            raw,
            QStringLiteral(R"(^\s*SB\s*:)"),
            QRegularExpression::CaseInsensitiveOption
        )
    ) {
        return QStringLiteral("Sideboard");
    }
    if (
        (kind == ImportKind::ArenaLike || kind == ImportKind::XmageLike)
        && !section.has_value()
    ) {
        return QStringLiteral("Mainboard");
    }
    return section;
}

bool isTextKind(ImportKind kind)
{
    return kind == ImportKind::SimpleDecklist
        || kind == ImportKind::ArenaLike
        || kind == ImportKind::MtgoLike
        || kind == ImportKind::XmageLike
        || kind == ImportKind::MwdeckLike;
}

} // namespace

ImporterOutput parseTextImport(
    const QString& text,
    ImportKind kind,
    const std::optional<ImportSource>& source
)
{
    const std::optional<QString> sourceId =
        source.has_value()
        ? std::optional<QString>(source->id)
        : std::nullopt;
    const std::optional<QString> sourcePath =
        source.has_value()
        ? source->sourcePath
        : std::nullopt;

    if (!isTextKind(kind)) {
        throw ImportFailureError(
            QStringLiteral("Selected importer does not accept deck text."),
            QStringLiteral("FORMAT_MISMATCH"),
            sourceId,
            sourcePath
        );
    }
    if (!adapterMatches(kind, text, source)) {
        throw ImportFailureError(
            QStringLiteral(
                "Text does not contain enough structure for the selected adapter."
            ),
            QStringLiteral("FORMAT_MISMATCH"),
            sourceId,
            sourcePath
        );
    }

    const QString resolvedSourceId =
        source.has_value()
        ? source->id
        : QStringLiteral("pasted-text");
    const int sourceOrder =
        source.has_value() ? source->order : 0;

    ImporterOutput output;
    std::optional<QString> currentSection;

    QString normalized = text;
    if (!normalized.isEmpty() && normalized.front() == QChar(0xfeff)) {
        normalized.remove(0, 1);
    }
    const QStringList lines = normalized.split(
        QRegularExpression(QStringLiteral(R"(\r\n|\n|\r)")),
        Qt::KeepEmptyParts
    );

    for (qsizetype index = 0; index < lines.size(); ++index) {
        const QString rawLine = lines[index];
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (
            kind == ImportKind::MwdeckLike
            && matches(
                line,
                QStringLiteral(
                    R"(Deck file for Magic Workstation|Magic Workstation)"
                ),
                QRegularExpression::CaseInsensitiveOption
            )
        ) {
            continue;
        }

        if (
            kind == ImportKind::XmageLike
            && matches(
                line,
                QStringLiteral(R"(^LAYOUT\s+)"),
                QRegularExpression::CaseInsensitiveOption
            )
        ) {
            const QRegularExpression layout(
                QStringLiteral(
                    R"(^LAYOUT\s+(MAIN|SIDEBOARD|COMMANDER|MAYBEBOARD)\s*$)"
                ),
                QRegularExpression::CaseInsensitiveOption
            );
            const auto match = layout.match(line);
            if (match.hasMatch()) {
                currentSection = sectionFor(match.captured(1));
            } else {
                output.warnings.push_back(ImportWarning{
                    .code = QStringLiteral("UNRECOGNIZED_SECTION"),
                    .message = QStringLiteral(
                        "Unrecognized XMage layout line."
                    ),
                    .sourceId = resolvedSourceId,
                    .sourceFilename =
                        source.has_value()
                        ? source->filename
                        : std::nullopt,
                    .sourcePath =
                        source.has_value()
                        ? source->sourcePath
                        : std::nullopt,
                    .line = static_cast<int>(index + 1),
                });
            }
            continue;
        }

        if (
            kind == ImportKind::MwdeckLike
            && matches(
                line,
                QStringLiteral(R"(^\s*\[[^\]]+\]\s*$)")
            )
        ) {
            currentSection =
                sectionFor(line.mid(1, line.size() - 2));
            if (!currentSection.has_value()) {
                output.warnings.push_back(ImportWarning{
                    .code = QStringLiteral("UNRECOGNIZED_SECTION"),
                    .message = QStringLiteral(
                        "Unrecognized MWS section."
                    ),
                    .sourceId = resolvedSourceId,
                    .sourceFilename =
                        source.has_value()
                        ? source->filename
                        : std::nullopt,
                    .sourcePath =
                        source.has_value()
                        ? source->sourcePath
                        : std::nullopt,
                    .line = static_cast<int>(index + 1),
                });
            }
            continue;
        }

        const std::optional<QString> recognizedSection =
            sectionFor(line);
        if (recognizedSection.has_value()) {
            currentSection = recognizedSection;
            continue;
        }

        if (
            kind == ImportKind::ArenaLike
            && matches(
                line,
                QStringLiteral(R"(^(?:Deck|Commander|Companion)\s*$)"),
                QRegularExpression::CaseInsensitiveOption
            )
        ) {
            currentSection =
                line.compare(
                    QStringLiteral("deck"),
                    Qt::CaseInsensitive
                ) == 0
                ? std::optional<QString>(
                    QStringLiteral("Mainboard")
                )
                : sectionFor(line);
            continue;
        }

        if (
            kind == ImportKind::MwdeckLike
            && (
                line.startsWith(QLatin1Char('#'))
                || line.startsWith(QStringLiteral("//"))
            )
        ) {
            continue;
        }

        const std::optional<ParsedCardLine> parsed =
            parseCardLine(line, kind);
        if (!parsed.has_value()) {
            output.warnings.push_back(ImportWarning{
                .code = QStringLiteral("UNPARSED_LINE"),
                .message = QStringLiteral(
                    "Unparsed line preserved as warning."
                ),
                .sourceId = resolvedSourceId,
                .sourceFilename =
                    source.has_value()
                    ? source->filename
                    : std::nullopt,
                .sourcePath =
                    source.has_value()
                    ? source->sourcePath
                    : std::nullopt,
                .line = static_cast<int>(index + 1),
            });
            continue;
        }

        const std::optional<QString> section =
            mapSectionForLine(
                kind,
                rawLine,
                currentSection
            );

        ImportedCardHint hint{
            .name = parsed->name,
            .setCode = parsed->setCode,
            .collectorNumber = parsed->collectorNumber,
            .section = section,
        };

        output.entries.push_back(ImportedEntry{
            .id = QStringLiteral("%1:line:%2")
                .arg(
                    resolvedSourceId,
                    QString::number(index + 1)
                ),
            .kind = ImportedEntryKind::DeckCard,
            .order =
                sourceOrder
                + static_cast<int>(output.entries.size()),
            .quantity = parsed->quantity,
            .sourceId = resolvedSourceId,
            .sourceFilename =
                source.has_value()
                ? source->filename
                : std::nullopt,
            .sourcePath =
                source.has_value()
                ? source->sourcePath
                : std::nullopt,
            .cardHint = std::move(hint),
            .section = section,
            .metadata = QJsonObject{
                {
                    QStringLiteral("rawLine"),
                    rawLine
                },
                {
                    QStringLiteral("parser"),
                    importKindName(kind)
                },
                {
                    QStringLiteral("line"),
                    static_cast<int>(index + 1)
                },
            },
        });
    }

    return output;
}

} // namespace tcgprint::imports
