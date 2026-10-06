#include "import/ImportDetection.h"

#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <array>
#include <utility>

namespace tcgprint::imports {
namespace {

using PatternOptions = QRegularExpression::PatternOptions;

bool matches(
    const QString& text,
    const QString& pattern,
    PatternOptions options = QRegularExpression::NoPatternOption
)
{
    return QRegularExpression(pattern, options).match(text).hasMatch();
}

std::optional<QString> normalizedText(const ImportDetectionInput& input)
{
    if (input.text.has_value()) {
        QString text = *input.text;
        if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
            text.remove(0, 1);
        }
        return text;
    }

    if (!input.bytes.has_value()) {
        return std::nullopt;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(*input.bytes);
    if (decoder.hasError()) {
        return std::nullopt;
    }
    if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
        text.remove(0, 1);
    }
    return text;
}

QString stripXmlPreamble(QString value)
{
    value.remove(QRegularExpression(QStringLiteral(R"(^\s+)")));
    value.remove(QRegularExpression(
        QStringLiteral(
            R"(^(?:(?:<\?xml\b[\s\S]*?\?>\s*)|(?:<!--[\s\S]*?-->\s*))+)"
        )
    ));
    return value;
}

void addCandidate(
    std::vector<ImportCandidate>& candidates,
    ImportKind kind,
    double confidence,
    QString reason,
    std::optional<QString> originalFormat = std::nullopt
)
{
    const auto found = std::find_if(
        candidates.begin(),
        candidates.end(),
        [kind](const ImportCandidate& candidate) {
            return candidate.kind == kind;
        }
    );

    if (found != candidates.end()) {
        found->confidence = std::max(found->confidence, confidence);
        found->reasons.push_back(std::move(reason));
        if (!found->originalFormat.has_value() && originalFormat.has_value()) {
            found->originalFormat = std::move(originalFormat);
        }
        return;
    }

    candidates.push_back(ImportCandidate{
        .kind = kind,
        .confidence = confidence,
        .reasons = QStringList{std::move(reason)},
        .originalFormat = std::move(originalFormat),
    });
}

bool isUrlInput(const QString& value, bool explicitUrlLike)
{
    if (explicitUrlLike) {
        return true;
    }

    return matches(
               value,
               QStringLiteral(R"(^\s*(?:https?|ftp|file|mailto|data):)"),
               QRegularExpression::CaseInsensitiveOption
           )
        || matches(
               value,
               QStringLiteral(R"(^\s*[a-z][a-z\d+.-]*://)"),
               QRegularExpression::CaseInsensitiveOption
           );
}

std::optional<ImportKind> mediaTypeKind(const std::optional<QString>& mediaType)
{
    if (!mediaType.has_value()) {
        return std::nullopt;
    }

    const QString normalized =
        mediaType->section(QLatin1Char(';'), 0, 0).trimmed().toLower();

    if (normalized.isEmpty()) return std::nullopt;
    if (normalized == QStringLiteral("image/svg+xml")) return ImportKind::Svg;
    if (
        normalized == QStringLiteral("image/png")
        || normalized == QStringLiteral("image/jpeg")
        || normalized == QStringLiteral("image/webp")
        || normalized == QStringLiteral("image/tiff")
    ) return ImportKind::Image;
    if (
        normalized == QStringLiteral("text/csv")
        || normalized == QStringLiteral("application/csv")
    ) return ImportKind::Csv;
    if (normalized == QStringLiteral("text/tab-separated-values")) return ImportKind::Tsv;
    if (
        normalized == QStringLiteral("application/json")
        || normalized == QStringLiteral("text/json")
    ) return ImportKind::Json;
    if (
        normalized == QStringLiteral("application/xml")
        || normalized == QStringLiteral("text/xml")
    ) return ImportKind::GenericXml;
    if (
        normalized == QStringLiteral("application/zip")
        || normalized == QStringLiteral("application/x-zip-compressed")
    ) return ImportKind::Zip;
    return std::nullopt;
}

bool isTiff(const QByteArray& bytes)
{
    if (bytes.size() < 4) {
        return false;
    }
    const auto byte = [&bytes](qsizetype index) {
        return static_cast<unsigned char>(bytes[index]);
    };
    return (
        byte(0) == 0x49
        && byte(1) == 0x49
        && (byte(2) == 0x2a || byte(2) == 0x2b)
        && byte(3) == 0x00
    ) || (
        byte(0) == 0x4d
        && byte(1) == 0x4d
        && byte(2) == 0x00
        && (byte(3) == 0x2a || byte(3) == 0x2b)
    );
}

std::vector<int> countUnquotedDelimiters(
    const QString& text,
    QChar delimiter
)
{
    std::vector<int> rows;
    int count = 0;
    bool inQuotes = false;
    const qsizetype limit = std::min<qsizetype>(
        text.size(),
        64 * 1024
    );

    for (qsizetype index = 0; index < limit; ++index) {
        const QChar character = text[index];
        if (character == QLatin1Char('"')) {
            if (
                inQuotes
                && index + 1 < limit
                && text[index + 1] == QLatin1Char('"')
            ) {
                ++index;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (!inQuotes && character == delimiter) {
            ++count;
        } else if (
            !inQuotes
            && (
                character == QLatin1Char('\n')
                || character == QLatin1Char('\r')
            )
        ) {
            if (count > 0) {
                rows.push_back(count);
            }
            count = 0;
            if (
                character == QLatin1Char('\r')
                && index + 1 < limit
                && text[index + 1] == QLatin1Char('\n')
            ) {
                ++index;
            }
        }
    }

    if (count > 0) {
        rows.push_back(count);
    }
    if (rows.size() > 12) {
        rows.resize(12);
    }
    return rows;
}

double tabularConfidence(const std::vector<int>& rows)
{
    if (rows.empty()) {
        return 0.0;
    }

    const auto consistentRows = std::count(
        rows.begin(),
        rows.end(),
        rows.front()
    );
    const double consistency =
        static_cast<double>(consistentRows)
        / static_cast<double>(rows.size());

    return 0.72
        + std::min(static_cast<double>(rows.front()) * 0.04, 0.08)
        + (rows.size() > 1 ? consistency * 0.08 : 0.0);
}

std::optional<QString> getExtension(
    const std::optional<QString>& filename
)
{
    if (!filename.has_value()) {
        return std::nullopt;
    }

    QString name = *filename;
    const qsizetype slash = std::max(
        name.lastIndexOf(QLatin1Char('/')),
        name.lastIndexOf(QLatin1Char('\\'))
    );
    if (slash >= 0) {
        name = name.mid(slash + 1);
    }

    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return std::nullopt;
    }
    return name.mid(dot + 1).toLower();
}

std::optional<ImportKind> extensionKind(const QString& extension)
{
    if (
        extension == QStringLiteral("png")
        || extension == QStringLiteral("jpg")
        || extension == QStringLiteral("jpeg")
        || extension == QStringLiteral("webp")
        || extension == QStringLiteral("tif")
        || extension == QStringLiteral("tiff")
    ) return ImportKind::Image;
    if (extension == QStringLiteral("svg")) return ImportKind::Svg;
    if (extension == QStringLiteral("txt")) return ImportKind::SimpleDecklist;
    if (extension == QStringLiteral("csv")) return ImportKind::Csv;
    if (extension == QStringLiteral("tsv")) return ImportKind::Tsv;
    if (extension == QStringLiteral("json")) return ImportKind::Json;
    if (extension == QStringLiteral("xml")) return ImportKind::GenericXml;
    if (extension == QStringLiteral("zip")) return ImportKind::Zip;
    if (extension == QStringLiteral("mwdeck")) return ImportKind::MwdeckLike;
    if (extension == QStringLiteral("dck")) return ImportKind::XmageLike;
    return std::nullopt;
}

void addTextCandidates(
    const QString& text,
    const std::optional<QString>& filename,
    std::vector<ImportCandidate>& candidates
)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QStringList lines = trimmed.split(
        QRegularExpression(QStringLiteral(R"(\r?\n)")),
        Qt::SkipEmptyParts
    );
    for (QString& line : lines) {
        line = line.trimmed();
    }
    lines.removeAll(QString());

    const std::vector<int> commaRows =
        countUnquotedDelimiters(text, QLatin1Char(','));
    const std::vector<int> tabRows =
        countUnquotedDelimiters(text, QLatin1Char('\t'));
    const bool isTabular =
        (!commaRows.empty() && commaRows.front() > 0)
        || (!tabRows.empty() && tabRows.front() > 0);
    const bool isStructured =
        matches(trimmed, QStringLiteral(R"(^[\[{<])"))
        || isUrlInput(trimmed, false);

    const PatternOptions ciMultiline =
        QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::MultilineOption;
    const PatternOptions multiline =
        QRegularExpression::MultilineOption;

    const bool hasMtgoMarker = matches(
        text,
        QStringLiteral(R"(^\s*(?:SB\s*:\s*\d+|SIDEBOARD\s*:\s*\d+)\s+)"),
        ciMultiline
    );
    const bool hasXmageMarker =
        matches(
            text,
            QStringLiteral(R"(^\s*LAYOUT\s+(?:MAIN|SIDEBOARD|COMMANDER|MAYBEBOARD)\s*$)"),
            ciMultiline
        )
        && matches(
            text,
            QStringLiteral(R"(^\s*LAYOUT\s+MAIN\s*$)"),
            ciMultiline
        );
    const bool mwdeckMarker = matches(
        text,
        QStringLiteral(R"(Deck file for Magic Workstation|Magic Workstation)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const bool bracketedSetLine = matches(
        text,
        QStringLiteral(R"(^(?:\d+\s*x?\s*)?\[[A-Za-z0-9]{2,8}\]\s*\S+)"),
        multiline
    );
    const auto extension = getExtension(filename);
    const bool hasMwsMarker =
        (mwdeckMarker && bracketedSetLine)
        || (
            extension.has_value()
            && *extension == QStringLiteral("mwdeck")
            && bracketedSetLine
        );
    const bool hasArenaMarker =
        matches(
            text,
            QStringLiteral(R"(^\s*(?:Deck|Commander|Companion)\s*$)"),
            ciMultiline
        )
        && matches(
            text,
            QStringLiteral(R"(^\s*\d+\s+.+\s+\([A-Za-z0-9]{2,8}\)\s+[A-Za-z0-9][A-Za-z0-9/-]*\s*$)"),
            multiline
        )
        && matches(
            text,
            QStringLiteral(R"(^\s*Deck\s*$)"),
            ciMultiline
        );
    const bool hasRecognizedAdapter =
        hasMtgoMarker
        || hasXmageMarker
        || hasMwsMarker
        || hasArenaMarker;

    if (!isStructured && !isTabular && !hasRecognizedAdapter) {
        addCandidate(
            candidates,
            ImportKind::SimpleDecklist,
            lines.size() == 1 ? 0.81 : 0.86,
            QStringLiteral("Text has decklist-compatible lines.")
        );
    }

    if (hasMtgoMarker) {
        addCandidate(
            candidates,
            ImportKind::MtgoLike,
            0.97,
            QStringLiteral("MTGO sideboard marker found.")
        );
    }

    if (hasXmageMarker) {
        addCandidate(
            candidates,
            ImportKind::XmageLike,
            0.97,
            QStringLiteral("XMage LAYOUT MAIN marker found.")
        );
    }

    if (hasMwsMarker) {
        addCandidate(
            candidates,
            ImportKind::MwdeckLike,
            mwdeckMarker ? 0.98 : 0.90,
            QStringLiteral("Magic Workstation markers found.")
        );
    }

    if (hasArenaMarker) {
        addCandidate(
            candidates,
            ImportKind::ArenaLike,
            0.98,
            QStringLiteral("Arena Deck and printing markers found.")
        );
    }

    if (
        extension.has_value()
        && *extension == QStringLiteral("dck")
        && matches(
            text,
            QStringLiteral(R"(^\s*(?:LAYOUT\s+MAIN|SB\s*:))"),
            ciMultiline
        )
    ) {
        addCandidate(
            candidates,
            ImportKind::XmageLike,
            0.90,
            QStringLiteral(".dck plus recognized deck structure found.")
        );
    }

    if (!commaRows.empty() && commaRows.front() > 0) {
        addCandidate(
            candidates,
            ImportKind::Csv,
            tabularConfidence(commaRows),
            QStringLiteral("Consistent unquoted CSV delimiters found.")
        );
    }
    if (!tabRows.empty() && tabRows.front() > 0) {
        addCandidate(
            candidates,
            ImportKind::Tsv,
            tabularConfidence(tabRows),
            QStringLiteral("Consistent unquoted TSV delimiters found.")
        );
    }

    if (matches(text, QStringLiteral(R"(^\s*[\[{])"))) {
        addCandidate(
            candidates,
            ImportKind::Json,
            0.84,
            QStringLiteral("Content starts with a JSON object or array.")
        );
    }

    const QString xmlText = stripXmlPreamble(text);
    if (
        matches(
            xmlText,
            QStringLiteral(R"(^<svg(?:\s|>))"),
            QRegularExpression::CaseInsensitiveOption
        )
    ) {
        addCandidate(
            candidates,
            ImportKind::Svg,
            0.99,
            QStringLiteral("SVG root element found."),
            QStringLiteral("svg")
        );
    } else if (
        matches(
            xmlText,
            QStringLiteral(R"(^<[A-Za-z_:][\w:.-]*(?:\s|>))")
        )
    ) {
        const bool isMpc =
            matches(
                xmlText,
                QStringLiteral(R"(^<order(?:\s|>))"),
                QRegularExpression::CaseInsensitiveOption
            )
            && matches(
                xmlText,
                QStringLiteral(R"(<fronts(?:\s|>))"),
                QRegularExpression::CaseInsensitiveOption
            )
            && matches(
                xmlText,
                QStringLiteral(R"(<card(?:\s|>))"),
                QRegularExpression::CaseInsensitiveOption
            )
            && matches(
                xmlText,
                QStringLiteral(R"(<(?:id|slots?|query|name)(?:\s|>))"),
                QRegularExpression::CaseInsensitiveOption
            );

        addCandidate(
            candidates,
            ImportKind::GenericXml,
            isMpc ? 0.82 : 0.86,
            QStringLiteral("XML root element found."),
            QStringLiteral("xml")
        );

        if (isMpc) {
            addCandidate(
                candidates,
                ImportKind::MpcAutofillXml,
                0.99,
                QStringLiteral("MPC Autofill order/fronts/card structure found."),
                QStringLiteral("xml")
            );
        }
    }
}

std::vector<ImportCandidate> applyExtensionHint(
    std::vector<ImportCandidate> candidates,
    const std::optional<QString>& filename,
    QStringList& reasons,
    const DetectionPolicy& policy
)
{
    const auto extension = getExtension(filename);
    if (!extension.has_value()) {
        return candidates;
    }

    const auto kind = extensionKind(*extension);
    if (!kind.has_value()) {
        return candidates;
    }

    QString extensionFormat = *extension;
    if (extensionFormat == QStringLiteral("jpg")) {
        extensionFormat = QStringLiteral("jpeg");
    } else if (extensionFormat == QStringLiteral("tif")) {
        extensionFormat = QStringLiteral("tiff");
    }

    const auto matching = std::find_if(
        candidates.begin(),
        candidates.end(),
        [kind](const ImportCandidate& candidate) {
            return candidate.kind == *kind;
        }
    );

    if (
        matching != candidates.end()
        && (
            *kind != ImportKind::Image
            || (
                matching->originalFormat.has_value()
                && *matching->originalFormat == extensionFormat
            )
        )
    ) {
        matching->confidence = std::min(
            0.99,
            matching->confidence + policy.extensionAdjustment
        );
        matching->reasons.push_back(
            QStringLiteral("File extension reinforces content evidence.")
        );
        return candidates;
    }

    if (matching != candidates.end() || !candidates.empty()) {
        const QString mismatch =
            QStringLiteral("File extension conflicts with content evidence.");
        reasons.push_back(mismatch);
        if (matching != candidates.end()) {
            matching->reasons.push_back(mismatch);
        }
    }

    return candidates;
}

bool containsKind(
    const std::vector<ImportCandidate>& candidates,
    ImportKind kind
)
{
    return std::any_of(
        candidates.begin(),
        candidates.end(),
        [kind](const ImportCandidate& candidate) {
            return candidate.kind == kind;
        }
    );
}

} // namespace

QString importKindName(ImportKind kind)
{
    switch (kind) {
    case ImportKind::Image: return QStringLiteral("image");
    case ImportKind::Svg: return QStringLiteral("svg");
    case ImportKind::SimpleDecklist: return QStringLiteral("simple-decklist");
    case ImportKind::ArenaLike: return QStringLiteral("arena-like");
    case ImportKind::MtgoLike: return QStringLiteral("mtgo-like");
    case ImportKind::XmageLike: return QStringLiteral("xmage-like");
    case ImportKind::MwdeckLike: return QStringLiteral("mwdeck-like");
    case ImportKind::Csv: return QStringLiteral("csv");
    case ImportKind::Tsv: return QStringLiteral("tsv");
    case ImportKind::Json: return QStringLiteral("json");
    case ImportKind::GenericXml: return QStringLiteral("generic-xml");
    case ImportKind::MpcAutofillXml: return QStringLiteral("mpc-autofill-xml");
    case ImportKind::Zip: return QStringLiteral("zip");
    case ImportKind::Url: return QStringLiteral("url");
    case ImportKind::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

ImportDetection detectImport(
    const ImportDetectionInput& input,
    const DetectionPolicy& policy
)
{
    std::vector<ImportCandidate> candidates;
    QStringList reasons;
    const std::optional<QString> text = normalizedText(input);
    std::optional<QString> signatureFormat;

    if (input.bytes.has_value() && !input.bytes->isEmpty()) {
        const QByteArray& bytes = *input.bytes;
        const auto byte = [&bytes](qsizetype index) {
            return static_cast<unsigned char>(bytes[index]);
        };

        static constexpr std::array<unsigned char, 8> pngSignature{
            137, 80, 78, 71, 13, 10, 26, 10
        };

        bool isPng =
            bytes.size() >= static_cast<qsizetype>(pngSignature.size());
        for (
            qsizetype i = 0;
            isPng
            && i < static_cast<qsizetype>(pngSignature.size());
            ++i
        ) {
            isPng =
                byte(i) == pngSignature[static_cast<std::size_t>(i)];
        }

        if (isPng) {
            signatureFormat = QStringLiteral("png");
        } else if (
            bytes.size() >= 3
            && byte(0) == 0xff
            && byte(1) == 0xd8
            && byte(2) == 0xff
        ) {
            signatureFormat = QStringLiteral("jpeg");
        } else if (
            bytes.size() >= 12
            && bytes.mid(0, 4) == QByteArrayLiteral("RIFF")
            && bytes.mid(8, 4) == QByteArrayLiteral("WEBP")
        ) {
            signatureFormat = QStringLiteral("webp");
        } else if (isTiff(bytes)) {
            signatureFormat = QStringLiteral("tiff");
        } else if (
            bytes.size() >= 4
            && byte(0) == 0x50
            && byte(1) == 0x4b
            && (
                byte(2) == 0x03
                || byte(2) == 0x05
                || byte(2) == 0x07
            )
            && (
                byte(3) == 0x04
                || byte(3) == 0x06
                || byte(3) == 0x08
            )
        ) {
            addCandidate(
                candidates,
                ImportKind::Zip,
                0.99,
                QStringLiteral("ZIP signature found in bytes."),
                QStringLiteral("zip")
            );
        }

        if (signatureFormat.has_value()) {
            addCandidate(
                candidates,
                ImportKind::Image,
                0.98,
                QStringLiteral("Raster signature found in bytes."),
                signatureFormat
            );
        }
    }

    if (
        !signatureFormat.has_value()
        && !containsKind(candidates, ImportKind::Zip)
        && text.has_value()
    ) {
        if (isUrlInput(*text, input.urlLike)) {
            addCandidate(
                candidates,
                ImportKind::Url,
                0.99,
                QStringLiteral(
                    "Input is URL-like; protocol and host validation are deferred."
                )
            );
        } else {
            addTextCandidates(*text, input.fileName, candidates);
        }
    }

    const std::optional<ImportKind> declaredKind =
        mediaTypeKind(input.mediaType);

    if (declaredKind.has_value()) {
        const bool hasSpecificContentEvidence =
            std::any_of(
                candidates.begin(),
                candidates.end(),
                [](const ImportCandidate& candidate) {
                    return candidate.kind != ImportKind::SimpleDecklist
                        && candidate.kind != ImportKind::Unknown;
                }
            );

        const QString trimmedText =
            text.has_value() ? text->trimmed() : QString();

        std::optional<ImportKind> structuredContentKind;
        if (
            (
                trimmedText.startsWith(QLatin1Char('{'))
                || trimmedText.startsWith(QLatin1Char('['))
            )
            && containsKind(candidates, ImportKind::Json)
        ) {
            structuredContentKind = ImportKind::Json;
        } else if (trimmedText.startsWith(QLatin1Char('<'))) {
            for (const ImportCandidate& candidate : candidates) {
                if (
                    candidate.kind == ImportKind::MpcAutofillXml
                    || candidate.kind == ImportKind::Svg
                    || candidate.kind == ImportKind::GenericXml
                ) {
                    structuredContentKind = candidate.kind;
                    break;
                }
            }
        }

        const bool specializedDeck =
            std::any_of(
                candidates.begin(),
                candidates.end(),
                [](const ImportCandidate& candidate) {
                    return candidate.kind == ImportKind::ArenaLike
                        || candidate.kind == ImportKind::MtgoLike
                        || candidate.kind == ImportKind::XmageLike
                        || candidate.kind == ImportKind::MwdeckLike;
                }
            );

        auto matching = std::find_if(
            candidates.begin(),
            candidates.end(),
            [declaredKind](const ImportCandidate& candidate) {
                return candidate.kind == *declaredKind;
            }
        );

        if (
            !hasSpecificContentEvidence
            && containsKind(candidates, ImportKind::SimpleDecklist)
        ) {
            candidates.clear();
            candidates.push_back(ImportCandidate{
                .kind = *declaredKind,
                .confidence = 0.94,
                .reasons = QStringList{
                    QStringLiteral("Content-Type supplies secondary evidence.")
                },
                .originalFormat =
                    *declaredKind == ImportKind::Svg
                    ? std::optional<QString>(QStringLiteral("svg"))
                    : std::nullopt,
            });
        } else if (
            matching != candidates.end()
            && !signatureFormat.has_value()
            && !specializedDeck
            && (
                !structuredContentKind.has_value()
                || *structuredContentKind == *declaredKind
            )
        ) {
            matching->confidence =
                std::max(matching->confidence, 0.99);
            matching->reasons.push_back(
                QStringLiteral("Content-Type confirms content evidence.")
            );
        } else if (
            matching == candidates.end()
            && structuredContentKind.has_value()
            && *structuredContentKind != *declaredKind
        ) {
            // Structured JSON/XML evidence is stronger than conflicting MIME.
        } else if (matching == candidates.end()) {
            addCandidate(
                candidates,
                *declaredKind,
                0.82,
                QStringLiteral("Content-Type supplies secondary evidence."),
                *declaredKind == ImportKind::Svg
                    ? std::optional<QString>(QStringLiteral("svg"))
                    : std::nullopt
            );
        }
    }

    std::vector<ImportCandidate> hinted =
        applyExtensionHint(
            std::move(candidates),
            input.fileName,
            reasons,
            policy
        );

    if (hinted.empty()) {
        reasons.push_back(
            QStringLiteral(
                "No recognized byte signature or text structure; extension alone is insufficient."
            )
        );
        const ImportCandidate unknown{
            .kind = ImportKind::Unknown,
            .confidence = 1.0,
            .reasons = QStringList{reasons.front()},
            .originalFormat = std::nullopt,
        };
        return ImportDetection{
            .status = ImportDetectionStatus::Unknown,
            .candidates = std::vector<ImportCandidate>{unknown},
            .selected = std::nullopt,
            .reasons = reasons,
        };
    }

    for (ImportCandidate& candidate : hinted) {
        candidate.confidence =
            std::clamp(candidate.confidence, 0.0, 1.0);
    }

    std::sort(
        hinted.begin(),
        hinted.end(),
        [](const ImportCandidate& left, const ImportCandidate& right) {
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return importKindName(left.kind)
                < importKindName(right.kind);
        }
    );

    const ImportCandidate& top = hinted.front();
    const ImportCandidate* second =
        hinted.size() > 1 ? &hinted[1] : nullptr;
    const bool hasPlausibleSecond =
        second != nullptr
        && second->confidence >= policy.ambiguousMinimum;
    const bool ambiguous =
        top.confidence < policy.autoSelectMinimum
        || (
            hasPlausibleSecond
            && top.confidence - second->confidence
                < policy.ambiguityMargin
        );

    if (ambiguous) {
        reasons.push_back(
            QStringLiteral(
                "Multiple plausible formats or insufficient confidence; require explicit choice."
            )
        );
        return ImportDetection{
            .status = ImportDetectionStatus::Ambiguous,
            .candidates = std::move(hinted),
            .selected = std::nullopt,
            .reasons = std::move(reasons),
        };
    }

    reasons.push_back(
        QStringLiteral("Format auto-selected from content evidence.")
    );

    return ImportDetection{
        .status = ImportDetectionStatus::AutoSelected,
        .candidates = hinted,
        .selected = top,
        .reasons = std::move(reasons),
    };
}

} // namespace tcgprint::imports
