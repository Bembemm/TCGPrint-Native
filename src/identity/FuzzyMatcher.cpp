#include "identity/FuzzyMatcher.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <numeric>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#include <unicode/ucol.h>
#include <unicode/ustring.h>

namespace tcgprint::identity {
namespace {

QString englishLowercase(const QString& value)
{
    if (value.isEmpty()) return {};
    if (value.size() > std::numeric_limits<int32_t>::max()) {
        throw std::length_error("Name normalization exceeds ICU string length limit");
    }
    const auto* source = reinterpret_cast<const UChar*>(value.utf16());
    const auto sourceLength = static_cast<int32_t>(value.size());
    UErrorCode status = U_ZERO_ERROR;
    const auto required = u_strToLower(nullptr, 0, source, sourceLength, "en_US", &status);
    if (U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) {
        throw std::runtime_error("Unable to size English Unicode lowercase result");
    }
    QString lowered;
    lowered.resize(required);
    status = U_ZERO_ERROR;
    u_strToLower(reinterpret_cast<UChar*>(lowered.data()), required,
                 source, sourceLength, "en_US", &status);
    if (U_FAILURE(status)) {
        throw std::runtime_error("Unable to lowercase English Unicode name");
    }
    return lowered;
}

QString normalize(const QString& value)
{
    auto normalized = value.normalized(QString::NormalizationForm_KD);
    // Diacritic is the oracle's binary Unicode property, not all combining
    // marks: spacing diacritics disappear; other marks become separators.
    normalized.remove(QRegularExpression(QStringLiteral("\\p{Diacritic}")));
    // QLocale uses NLS on Windows, which lacks contextual final-sigma casing.
    // ICU preserves the oracle's en-US Unicode casing on every platform.
    normalized = englishLowercase(normalized);
    normalized.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
                       QStringLiteral(" "));
    return normalized.simplified();
}

qsizetype editDistance(const QList<uint>& left, const QList<uint>& right)
{
    std::vector<qsizetype> previous(static_cast<std::size_t>(right.size()) + 1);
    std::vector<qsizetype> current(previous.size());
    std::iota(previous.begin(), previous.end(), qsizetype{0});
    for (qsizetype row = 1; row <= left.size(); ++row) {
        current[0] = row;
        for (qsizetype column = 1; column <= right.size(); ++column) {
            const auto index = static_cast<std::size_t>(column);
            current[index] = std::min({
                current[index - 1] + 1,
                previous[index] + 1,
                previous[index - 1] + (left[row - 1] == right[column - 1] ? 0 : 1),
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

QString stableKey(const FuzzyCandidate& candidate)
{
    if (candidate.id) return *candidate.id;
    // Equivalent to JSON.stringify for this small candidate type (name only).
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("name"), candidate.name},
    }).toJson(QJsonDocument::Compact));
}

using EnglishCollator = std::unique_ptr<UCollator, decltype(&ucol_close)>;

EnglishCollator makeEnglishCollator()
{
    // Intl.localeCompare("en") uses Unicode linguistic collation. QCollator's
    // Windows NLS backend instead uses word sort (e.g. coop before co-op), so
    // use ICU explicitly on every platform to preserve the oracle's ordering.
    UErrorCode status = U_ZERO_ERROR;
    EnglishCollator collator(ucol_open("en", &status), &ucol_close);
    if (U_FAILURE(status) || !collator) {
        throw std::runtime_error("Unable to initialize English Unicode collation");
    }
    ucol_setAttribute(collator.get(), UCOL_STRENGTH, UCOL_TERTIARY, &status);
    ucol_setAttribute(collator.get(), UCOL_CASE_FIRST, UCOL_OFF, &status);
    ucol_setAttribute(collator.get(), UCOL_CASE_LEVEL, UCOL_OFF, &status);
    ucol_setAttribute(collator.get(), UCOL_NUMERIC_COLLATION, UCOL_OFF, &status);
    ucol_setAttribute(collator.get(), UCOL_ALTERNATE_HANDLING, UCOL_NON_IGNORABLE, &status);
    ucol_setAttribute(collator.get(), UCOL_NORMALIZATION_MODE, UCOL_ON, &status);
    if (U_FAILURE(status)) {
        throw std::runtime_error("Unable to configure English Unicode collation");
    }
    return collator;
}

int compareEnglish(const UCollator* collator, const QString& left, const QString& right)
{
    if (left.size() > std::numeric_limits<int32_t>::max()
        || right.size() > std::numeric_limits<int32_t>::max()) {
        throw std::length_error("Candidate collation exceeds ICU string length limit");
    }
    return ucol_strcoll(collator,
        reinterpret_cast<const UChar*>(left.utf16()), static_cast<int32_t>(left.size()),
        reinterpret_cast<const UChar*>(right.utf16()), static_cast<int32_t>(right.size()));
}

struct RankedEntry final
{
    RankedFuzzyCandidate match;
    QString key;
};

} // namespace

FuzzyMatch fuzzyMatchName(
    const QString& query,
    const std::vector<FuzzyCandidate>& candidates,
    const IdentityResolutionPolicy& policy
)
{
    const auto normalizedQuery = normalize(query);
    if (static_cast<std::size_t>(normalizedQuery.size()) < policy.minimumQueryLength
        || candidates.empty()) {
        return {};
    }

    const auto queryPoints = normalizedQuery.toUcs4();
    std::vector<RankedEntry> ranked;
    ranked.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto normalizedName = normalize(candidate.name);
        // QString::size mirrors JS string.length (UTF-16 units), whereas
        // toUcs4 mirrors Array.from(string) used by the oracle's edit distance.
        const auto denominator = std::max({normalizedQuery.size(), normalizedName.size(), qsizetype{1}});
        const double score = normalizedQuery == normalizedName ? 1.0 : std::max(
            0.0, 1.0 - static_cast<double>(editDistance(queryPoints, normalizedName.toUcs4()))
                / static_cast<double>(denominator)
        );
        ranked.push_back({{candidate, score}, stableKey(candidate)});
    }

    const auto collator = makeEnglishCollator();
    std::sort(ranked.begin(), ranked.end(), [&collator](const auto& left, const auto& right) {
        if (left.match.score != right.match.score) return left.match.score > right.match.score;
        const auto& a = left.match.candidate;
        const auto& b = right.match.candidate;
        if (const auto order = compareEnglish(collator.get(), a.name, b.name); order != 0) return order < 0;
        if (const auto order = compareEnglish(collator.get(), left.key, right.key); order != 0) return order < 0;
        // Locale collation can equate distinct representations. Complete the
        // order with raw UTF-16 name/key and absent-ID before present-ID.
        // Identical value candidates are interchangeable; no input index/hash.
        if (const auto order = QString::compare(a.name, b.name, Qt::CaseSensitive); order != 0) return order < 0;
        if (const auto order = QString::compare(left.key, right.key, Qt::CaseSensitive); order != 0) return order < 0;
        return a.id.has_value() < b.id.has_value();
    });

    const auto& top = ranked.front().match;
    if (top.score == 1.0) {
        return {FuzzyMatchStatus::Resolved, 1.0, FuzzyMatchReason::ExactName,
                top.candidate, {top}};
    }
    if (top.score < policy.fuzzySuggestThreshold) {
        return {FuzzyMatchStatus::Unresolved, top.score, FuzzyMatchReason::BelowThreshold,
                std::nullopt, {}};
    }

    std::vector<RankedFuzzyCandidate> close;
    for (const auto& entry : ranked) {
        if (close.size() >= policy.maximumCandidates) break;
        if (entry.match.score >= policy.fuzzySuggestThreshold
            && top.score - entry.match.score <= policy.ambiguousMargin) {
            close.push_back(entry.match);
        }
    }
    if (close.size() > 1) {
        return {FuzzyMatchStatus::Ambiguous, top.score, FuzzyMatchReason::CompetingCloseMatches,
                top.candidate, std::move(close)};
    }
    return {FuzzyMatchStatus::Suggested, top.score, FuzzyMatchReason::ClosestName,
            top.candidate, {top}};
}

} // namespace tcgprint::identity
