# Pure fuzzy name matching

`tcgprint::identity::fuzzyMatchName` accepts a query, value candidates containing
`QString name` and optional `QString id`, and an optional policy. It returns a
value result without network requests, file access, artwork handling or domain
mutation. It is not connected to `IdentityResolver` yet.

Behavioral source: [web matcher](https://github.com/Bembemm/TCGPrint/blob/7f2308764d3f4691d662e29d3ad0cc9ee8535a1b/core/cards/fuzzy-matcher.ts),
[policy](https://github.com/Bembemm/TCGPrint/blob/7f2308764d3f4691d662e29d3ad0cc9ee8535a1b/core/cards/identity-policy.ts)
and [tests](https://github.com/Bembemm/TCGPrint/blob/7f2308764d3f4691d662e29d3ad0cc9ee8535a1b/tests/core/cards/fuzzy-matcher.test.ts).

The central `IdentityResolutionPolicyDefault` holds threshold `0.72`, ambiguity
margin `0.04`, minimum normalized query length `2`, and maximum close candidates
`5`. Callers can customize a copy.

Normalization uses Qt NFKD, removes the Unicode `Diacritic` binary property via
`QRegularExpression`, applies explicit English/US lowercase via `QLocale`, and
replaces runs outside Unicode letter/number categories with spaces, then trims
and collapses spaces. Removing all combining marks instead would change the
oracle: non-diacritic marks become separators, while spacing diacritics may be
removed. NFKD precedes this removal.

Levenshtein works on `QString::toUcs4()` code points. The minimum length and score
denominator deliberately use UTF-16 units (`QString::size()`), matching
JavaScript `string.length`. Equal normalized names score `1`; otherwise score is
`max(0, 1 - distance / max(query.length, name.length, 1))`.

Ranking uses descending score, English ICU collation of the original name,
then of the ID (or compact serialized `{name}` when absent). Numeric sorting is
off, punctuation participates, and case/accent differences participate. ICU is
an explicit build dependency because Qt's Windows `QCollator` uses NLS word
sorting: `coop` precedes `co-op`, reversing the oracle's English ordering. ICU
avoids that platform-specific behavior. Linux needs the ICU development package
(e.g. `libicu-dev`); Windows CI installs `icu:x64-windows-static-md` through vcpkg.
CMake links ICU i18n, uc and data, including static-library builds.

Two deliberate ordering refinements make the native value result deterministic:

- ID/fallback collation also uses explicit English, where the web leaves that
  locale implicit in the runtime.
- When linguistic name and key comparisons both tie, raw UTF-16 name and key,
  then absent-ID before present-ID, complete the order. The web's stable sort
  can preserve input order for canonically equivalent but distinct values.
  Identical candidate values are interchangeable, including duplicate entries.

Unicode normalization/collation tables follow the installed Qt/ICU versions,
as the web follows its JavaScript runtime's Unicode/ICU version. There is no
custom ASCII approximation or simplified edit-distance algorithm.

Short queries and empty inputs return score zero and no candidates. Exact
normalized names return only the top candidate. Below-threshold matches retain
the top score but no candidate/list. Close matches must satisfy both
`score >= threshold` and `top.score - score <= margin`, then are capped. More
than one yields Ambiguous; otherwise Suggested returns the top alone, including
when a custom maximum is zero. No input deduplication is performed by this layer.
