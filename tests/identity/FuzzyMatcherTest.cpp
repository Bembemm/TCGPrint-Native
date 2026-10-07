#include "identity/FuzzyMatcher.h"

#include <QLocale>
#include <QtTest>

#include <algorithm>

using namespace tcgprint::identity;

namespace {
const std::vector<FuzzyCandidate> choices{
    {QStringLiteral("Sol Ring"), QStringLiteral("sol")},
    {QStringLiteral("Sol Rung"), QStringLiteral("rung")},
    {QStringLiteral("Lightning Bolt"), QStringLiteral("bolt")},
};
}

class FuzzyMatcherTest final : public QObject
{
    Q_OBJECT

private slots:
    void centralPolicy()
    {
        QCOMPARE(IdentityResolutionPolicyDefault.fuzzySuggestThreshold, 0.72);
        QCOMPARE(IdentityResolutionPolicyDefault.ambiguousMargin, 0.04);
        QCOMPARE(IdentityResolutionPolicyDefault.minimumQueryLength, std::size_t{2});
        QCOMPARE(IdentityResolutionPolicyDefault.maximumCandidates, std::size_t{5});
    }

    void exactName()
    {
        const auto result = fuzzyMatchName(QStringLiteral("Sol Ring"), choices);
        QCOMPARE(result.status, FuzzyMatchStatus::Resolved);
        QCOMPARE(result.score, 1.0);
        QCOMPARE(result.reason, FuzzyMatchReason::ExactName);
        QVERIFY(result.candidate == choices[0]);
        QVERIFY(result.candidates == std::vector<RankedFuzzyCandidate>({{choices[0], 1.0}}));
    }

    void simpleTypo()
    {
        const auto result = fuzzyMatchName(QStringLiteral("Sol Rng"), {choices[0], choices[2]});
        QCOMPARE(result.status, FuzzyMatchStatus::Suggested);
        QCOMPARE(result.reason, FuzzyMatchReason::ClosestName);
        QVERIFY(result.candidate == choices[0]);
        QCOMPARE(result.score, 0.875);
        QVERIFY(result.score > IdentityResolutionPolicyDefault.fuzzySuggestThreshold);
        QVERIFY(result.candidates == std::vector<RankedFuzzyCandidate>({{choices[0], 0.875}}));
    }

    void closeCompetition()
    {
        const auto result = fuzzyMatchName(QStringLiteral("Sol Rng"), choices);
        QCOMPARE(result.status, FuzzyMatchStatus::Ambiguous);
        QCOMPARE(result.reason, FuzzyMatchReason::CompetingCloseMatches);
        QCOMPARE(result.score, 0.875);
        QVERIFY(result.candidate == choices[0]);
        QVERIFY(result.candidates == std::vector<RankedFuzzyCandidate>(
            {{choices[0], 0.875}, {choices[1], 0.875}}));
    }

    void belowThreshold()
    {
        const auto result = fuzzyMatchName(QStringLiteral("zzzzzz something"), choices);
        QCOMPARE(result.status, FuzzyMatchStatus::Unresolved);
        QCOMPARE(result.reason, FuzzyMatchReason::BelowThreshold);
        QVERIFY(result.score > 0.0); // Preserve top score even on a miss.
        QVERIFY(result.score < IdentityResolutionPolicyDefault.fuzzySuggestThreshold);
        QVERIFY(!result.candidate);
        QVERIFY(result.candidates.empty());
    }

    void customPolicy()
    {
        auto policy = IdentityResolutionPolicyDefault;
        policy.fuzzySuggestThreshold = 0.9;
        const auto result = fuzzyMatchName(QStringLiteral("Sol Rng"), {choices[0]}, policy);
        QCOMPARE(result.status, FuzzyMatchStatus::Unresolved);
        QCOMPARE(result.reason, FuzzyMatchReason::BelowThreshold);
        QCOMPARE(result.score, 0.875);
        QVERIFY(!result.candidate);
        QVERIFY(result.candidates.empty());
        // Exact normalized names precede threshold checks in the oracle.
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Ring"), choices, policy).status,
                 FuzzyMatchStatus::Resolved);
    }

    void shortQueryAndEmptyCandidates()
    {
        for (const auto& query : {QString(), QStringLiteral(" S! "), QStringLiteral("---")}) {
            const auto result = fuzzyMatchName(query, {{QStringLiteral("s"), std::nullopt}});
            QVERIFY(result == FuzzyMatch{});
        }
        QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), {}) == FuzzyMatch{});
        QVERIFY(fuzzyMatchName(QStringLiteral("s"), {}) == FuzzyMatch{});
        auto policy = IdentityResolutionPolicyDefault;
        policy.minimumQueryLength = 9;
        QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), choices, policy) == FuzzyMatch{});
        QCOMPARE(fuzzyMatchName(QStringLiteral("ab"), {{QStringLiteral("ab"), std::nullopt}}).status,
                 FuzzyMatchStatus::Resolved);
    }

    void maximumCandidates()
    {
        std::vector<FuzzyCandidate> candidates;
        for (const auto& name : {"Sol Rang", "Sol Reng", "Sol Ring", "Sol Rong", "Sol Rung", "Sol Ryng"}) {
            candidates.push_back({QString::fromLatin1(name), std::nullopt});
        }
        const auto result = fuzzyMatchName(QStringLiteral("Sol Rng"), candidates);
        QCOMPARE(result.status, FuzzyMatchStatus::Ambiguous);
        QCOMPARE(result.candidates.size(), std::size_t{5});
        QCOMPARE(result.candidates.front().candidate.name, QStringLiteral("Sol Rang"));
        QCOMPARE(result.candidates.back().candidate.name, QStringLiteral("Sol Rung"));
        auto policy = IdentityResolutionPolicyDefault;
        policy.maximumCandidates = 2;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), candidates, policy).candidates.size(), std::size_t{2});
        policy.maximumCandidates = 1;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), candidates, policy).status, FuzzyMatchStatus::Suggested);
        // Like slice(0, 0), zero disables ambiguity, not the suggested top result.
        policy.maximumCandidates = 0;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), candidates, policy).candidates.size(), std::size_t{1});
    }

    void normalizedExact_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("name");
        QTest::newRow("accent") << QStringLiteral("Eowyn") << QString::fromUtf8("Éowyn");
        QTest::newRow("decomposed") << QStringLiteral("E\u0301owyn") << QStringLiteral("Eowyn");
        QTest::newRow("punctuation") << QStringLiteral(" \tSol---RING /\n") << QStringLiteral("Sol Ring");
        QTest::newRow("compatibility") << QStringLiteral("\uff33\uff4f\uff4c Ring") << QStringLiteral("Sol Ring");
        QTest::newRow("ligature") << QStringLiteral("\ufb01re") << QStringLiteral("fire");
        QTest::newRow("spacing-diacritic") << QStringLiteral("Sol\u02c8 Ring") << QStringLiteral("Sol Ring");
        QTest::newRow("non-diacritic-mark") << QStringLiteral("ab\u034fcd") << QStringLiteral("ab cd");
        QTest::newRow("all-unicode-numbers") << QStringLiteral("\u4e00\u3007") << QStringLiteral("\u4e00\u3007");
        QTest::newRow("greek-final-sigma") << QStringLiteral("\u039f\u03a3") << QStringLiteral("\u03bf\u03c2");
    }

    void normalizedExact()
    {
        QFETCH(QString, query);
        QFETCH(QString, name);
        const FuzzyCandidate candidate{name, std::nullopt};
        const auto result = fuzzyMatchName(query, {candidate});
        QCOMPARE(result.status, FuzzyMatchStatus::Resolved);
        QCOMPARE(result.reason, FuzzyMatchReason::ExactName);
        QCOMPARE(result.score, 1.0);
        QVERIFY(result.candidate == candidate);
    }

    void unicodeDistance_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("name");
        QTest::addColumn<double>("score");
        QTest::newRow("cjk") << QStringLiteral("\u706b\u6c34\u6728\u571f")
            << QStringLiteral("\u706b\u6c34\u6728\u91d1") << 0.75;
        // Both are supplementary letters: two code points / four UTF-16 units.
        QTest::newRow("supplementary") << QStringLiteral("\U00010428\U00010429")
            << QStringLiteral("\U0001042a\U0001042b") << 0.5;
        QTest::newRow("insertion") << QStringLiteral("Sol Rng") << QStringLiteral("Sol Ring") << 0.875;
        QTest::newRow("deletion") << QStringLiteral("Sol Ring") << QStringLiteral("Sol Rng") << 0.875;
        QTest::newRow("substitution") << QStringLiteral("Sol Rang") << QStringLiteral("Sol Ring") << 0.875;
        QTest::newRow("zero") << QStringLiteral("abcd") << QStringLiteral("wxyz") << 0.0;
        QTest::newRow("empty-name") << QStringLiteral("abcd") << QString() << 0.0;
    }

    void unicodeDistance()
    {
        QFETCH(QString, query);
        QFETCH(QString, name);
        QFETCH(double, score);
        QCOMPARE(fuzzyMatchName(query, {{name, std::nullopt}}).score, score);
        // One supplementary letter meets JS's default minimum length of two.
        QCOMPARE(fuzzyMatchName(QStringLiteral("\U00010428"),
            {{QStringLiteral("\U00010428"), std::nullopt}}).status, FuzzyMatchStatus::Resolved);
    }

    void policyBoundaries()
    {
        auto policy = IdentityResolutionPolicyDefault;
        policy.fuzzySuggestThreshold = 0.875;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), {choices[0]}, policy).status,
                 FuzzyMatchStatus::Suggested);
        policy.ambiguousMargin = 0;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), choices, policy).status,
                 FuzzyMatchStatus::Ambiguous);
        // Best = 0.875, second = 0.75. Exercise inclusive margin and threshold.
        const std::vector<FuzzyCandidate> alternatives{choices[0], {QStringLiteral("Sal Rang"), std::nullopt}};
        policy.fuzzySuggestThreshold = 0.75;
        policy.ambiguousMargin = 0.125;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), alternatives, policy).status, FuzzyMatchStatus::Ambiguous);
        policy.ambiguousMargin = 0.124;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), alternatives, policy).status, FuzzyMatchStatus::Suggested);
        policy.ambiguousMargin = 0.125;
        policy.fuzzySuggestThreshold = 0.751;
        QCOMPARE(fuzzyMatchName(QStringLiteral("Sol Rng"), alternatives, policy).status, FuzzyMatchStatus::Suggested);
    }

    void deterministicRanking()
    {
        // Equal score: English name order first, IDs second, serialized name if absent.
        std::vector<FuzzyCandidate> candidates{
            {QStringLiteral("Sol Rung"), QStringLiteral("a")},
            {QStringLiteral("Sol Ring"), QStringLiteral("b")},
            {QStringLiteral("Sol Ring"), QStringLiteral("a")},
            {QStringLiteral("Sol Ring"), std::nullopt},
        };
        const auto expected = fuzzyMatchName(QStringLiteral("Sol Rng"), candidates);
        QCOMPARE(expected.status, FuzzyMatchStatus::Ambiguous);
        QCOMPARE(expected.candidates.size(), std::size_t{4});
        QVERIFY(expected.candidates[0].candidate == candidates[3]);
        QVERIFY(expected.candidates[1].candidate == candidates[2]);
        QVERIFY(expected.candidates[2].candidate == candidates[1]);
        QVERIFY(expected.candidates[3].candidate == candidates[0]);
        const auto before = candidates;
        std::vector<int> order{0, 1, 2, 3};
        do {
            std::vector<FuzzyCandidate> permuted;
            for (const auto index : order) permuted.push_back(candidates[index]);
            QVERIFY(fuzzyMatchName(QStringLiteral("Sol Rng"), permuted) == expected);
            QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), permuted).candidate == candidates[3]);
        } while (std::next_permutation(order.begin(), order.end()));
        QVERIFY(candidates == before);
    }

    void punctuationCollation()
    {
        // ICU/Intl English string collation differs from Windows NLS word sort.
        std::vector<FuzzyCandidate> candidates{
            {QStringLiteral("coop"), std::nullopt},
            {QStringLiteral("co-op"), std::nullopt},
        };
        const auto expected = fuzzyMatchName(QStringLiteral("cooop"), candidates);
        QCOMPARE(expected.status, FuzzyMatchStatus::Ambiguous);
        QCOMPARE(expected.score, 0.8);
        QVERIFY(expected.candidate == candidates[1]);
        std::reverse(candidates.begin(), candidates.end());
        QVERIFY(fuzzyMatchName(QStringLiteral("cooop"), candidates) == expected);
        candidates = {
            {QStringLiteral("Sol Ring"), QStringLiteral("coop")},
            {QStringLiteral("Sol Ring"), QStringLiteral("co-op")},
        };
        QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), candidates).candidate == candidates[1]);
        std::reverse(candidates.begin(), candidates.end());
        QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), candidates).candidate == candidates[0]);
    }

    void englishCollationAndFinalTie()
    {
        // en collation places lowercase before uppercase, unlike raw UTF-16.
        std::vector<FuzzyCandidate> candidates{
            {QStringLiteral("Sol Ring"), QStringLiteral("upper")},
            {QStringLiteral("sol ring"), QStringLiteral("lower")},
        };
        const auto expected = fuzzyMatchName(QStringLiteral("Sol Ring"), candidates);
        QVERIFY(expected.candidate == candidates[1]);
        std::reverse(candidates.begin(), candidates.end());
        QVERIFY(fuzzyMatchName(QStringLiteral("Sol Ring"), candidates) == expected);
        // Canonically equivalent names/IDs collate equal. Raw UTF-16 completes
        // the order where the web stable sort otherwise retains input order.
        candidates = {
            {QStringLiteral("\u00c9owyn"), QStringLiteral("\u00e9")},
            {QStringLiteral("E\u0301owyn"), QStringLiteral("e\u0301")},
        };
        const auto tied = fuzzyMatchName(QStringLiteral("Eowyn"), candidates);
        QVERIFY(tied.candidate == candidates[1]);
        std::reverse(candidates.begin(), candidates.end());
        QVERIFY(fuzzyMatchName(QStringLiteral("Eowyn"), candidates) == tied);
    }
};

QTEST_GUILESS_MAIN(FuzzyMatcherTest)
#include "FuzzyMatcherTest.moc"
