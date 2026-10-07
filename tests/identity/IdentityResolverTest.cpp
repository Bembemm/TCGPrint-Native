#include <QtTest>

#include "identity/IdentityResolver.h"
#include "identity/ScryfallIdentityClient.h"

#include <array>
#include <type_traits>

using namespace tcgprint;
using cards::IdentityResolutionMethod;
using cards::IdentityResolutionStatus;
using identity::ScryfallIdentityLookupErrorKind;

namespace {

identity::ScryfallIdentityCard printing()
{
    return {
        .id = QStringLiteral("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"),
        .oracleId = QStringLiteral("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"),
        .name = QStringLiteral("Sol Ring"),
        .layout = QStringLiteral("normal"),
        .setCode = QStringLiteral("cmm"),
        .collectorNumber = QStringLiteral("396"),
        .lang = QStringLiteral("en"),
        .digital = false,
        .promo = false,
        .fullArt = false,
        .imageStatus = QStringLiteral("highres_scan"),
        .faceNames = {QStringLiteral("Sol Ring")},
    };
}

cards::WorkingCard workingCard()
{
    cards::WorkingCard card;
    card.id = "working-stable";
    card.displayName = "Imported label";
    card.quantity = 6;
    card.order = 8;
    card.section = "Sideboard";
    card.importSource = {
        .sourceId = "upload",
        .filename = "Island.png",
        .importKind = "image",
        .entryKind = "deck-card",
    };
    card.identityHints = {
        .name = "Island",
        .setCode = "m21",
        .collectorNumber = "265",
        .scryfallId = "explicit-id",
    };
    card.identity = cards::CardIdentity{
        .provider = "manual",
        .id = "old-identity",
        .name = "Old identity",
    };
    card.identityResolution = {
        .status = IdentityResolutionStatus::Suggested,
        .method = IdentityResolutionMethod::Fuzzy,
        .query = "stale query",
        .confidence = 0.8,
        .candidates = {{*card.identity, 0.8, "old candidate"}},
        .confirmed = false,
    };
    card.faces = {
        {.id = "front", .side = cards::CardFaceSide::Front,
         .name = "Front", .importedAssetId = "local-front",
         .providerSlots = {"A1"}},
        {.id = "back", .side = cards::CardFaceSide::Back,
         .name = "Back", .importedAssetId = "local-back",
         .providerSlots = {"B1"}},
    };
    card.selectedFrontArtwork = cards::SelectedArtwork{
        .candidateId = "upload:front",
        .source = cards::ArtworkSource::Upload,
        .identityId = "old-identity",
        .faceId = cards::CardFaceSide::Front,
        .providerAssetId = "front-provider",
        .selectedArtworkId = "front-selection",
        .selectionPolicy = "user-selected",
    };
    card.selectedBackArtwork = cards::SelectedArtwork{
        .candidateId = "mpc:back",
        .source = cards::ArtworkSource::Mpc,
        .identityId = "old-identity",
        .faceId = cards::CardFaceSide::Back,
        .providerAssetId = "back-provider",
        .selectedArtworkId = "back-selection",
        .selectionPolicy = "default",
    };
    card.backMode = cards::BackMode::Manual;
    card.backModeSelectionPolicy = cards::BackModeSelectionPolicy::Explicit;
    card.manualBackAsset = cards::BackLibraryAssetReference{
        .assetId = "library-back", .sha256 = "hash", .format = "png",
    };
    card.manualBackArtwork = card.selectedBackArtwork;
    card.localArtworkIds = {"local-front", "local-back"};
    card.mpcReferences = {{
        .faceId = cards::CardFaceSide::Back,
        .importedAssetId = "mpc-reference",
        .providerAssetId = "mpc-provider",
        .selectedArtworkId = "mpc-selection",
        .referenceOrigin = cards::MpcReferenceOrigin::OrderImport,
        .providerCardType = cards::MpcProviderCardType::Cardback,
        .providerSlots = {"B1"},
        .availableLocally = true,
    }};
    card.sharedMpcCardback = cards::WorkingCardSharedMpcCardback{
        .importedAssetId = "shared-back",
        .providerAssetId = "shared-provider",
        .selectedArtworkId = "shared-selection",
        .originalFormat = "png",
        .availableLocally = true,
        .provenance = {.sourceId = "order", .sourceFilename = "order.xml"},
    };
    card.faceAssociations = {{
        .slot = "paired", .frontAssetId = "local-front",
        .backAssetId = "local-back", .confidence = 0.9,
        .reason = "import", .accepted = true,
    }};
    return card;
}

class FakeLookup final : public identity::ScryfallIdentityLookup
{
public:
    std::array<std::optional<ScryfallIdentityLookupErrorKind>, 3> errors{};
    identity::ScryfallIdentityCard found = printing();
    mutable QStringList calls;
    mutable QString id;
    mutable QString set;
    mutable QString collector;
    mutable std::optional<QString> language;
    mutable QString name;
    bool unexpectedError{false};

    identity::ScryfallIdentityCard lookupById(const QString& value) const override
    {
        calls.append(QStringLiteral("id"));
        id = value;
        return reply(0);
    }

    identity::ScryfallIdentityCard lookupBySetCollector(
        const QString& setCode, const QString& collectorNumber,
        const std::optional<QString>& lang
    ) const override
    {
        calls.append(QStringLiteral("set-collector"));
        set = setCode;
        collector = collectorNumber;
        language = lang;
        return reply(1);
    }

    identity::ScryfallIdentityCard lookupByExactName(const QString& value) const override
    {
        calls.append(QStringLiteral("exact-name"));
        name = value;
        return reply(2);
    }

private:
    identity::ScryfallIdentityCard reply(std::size_t stage) const
    {
        if (unexpectedError) {
            throw std::runtime_error("unexpected lookup failure");
        }
        if (errors[stage]) {
            throw identity::ScryfallIdentityLookupError(
                *errors[stage], "lookup failed", 404
            );
        }
        return found;
    }
};

// Compare every field except the two that this block is allowed to change.
void verifyUnrelatedFields(
    cards::WorkingCard actual, const cards::WorkingCard& original
)
{
    actual.identity = original.identity;
    actual.identityResolution = original.identityResolution;
    QVERIFY(actual == original);
}

void verifyResolved(
    const cards::WorkingCard& card, IdentityResolutionMethod method
)
{
    QVERIFY(card.identity.has_value());
    QCOMPARE(card.identity->provider, std::string("scryfall"));
    QCOMPARE(card.identity->name, std::string("Sol Ring"));
    QCOMPARE(card.identity->resolutionMethod, method);
    QCOMPARE(card.identity->confidence, 1.0);
    QCOMPARE(card.identityResolution.status, IdentityResolutionStatus::Resolved);
    QVERIFY(card.identityResolution.method == method);
    QVERIFY(card.identityResolution.confidence == 1.0);
    QVERIFY(card.identityResolution.query == card.identityHints.name);
    QVERIFY(card.identityResolution.candidates.empty());
    QVERIFY(!card.identityResolution.confirmed);
}

} // namespace

class IdentityResolverTest final : public QObject
{
    Q_OBJECT

private slots:
    void idTakesPriorityAndPreservesUnrelatedFields()
    {
        static_assert(std::is_base_of_v<identity::ScryfallIdentityLookup,
                                      identity::ScryfallIdentityClient>);
        FakeLookup lookup;
        const auto original = workingCard();
        const auto before = original;
        const auto result = identity::IdentityResolver(lookup).resolve(original);
        QCOMPARE(lookup.calls, QStringList({"id"}));
        QCOMPARE(lookup.id, QStringLiteral("explicit-id"));
        verifyResolved(result, IdentityResolutionMethod::ScryfallId);
        verifyUnrelatedFields(result, original);
        QVERIFY(original == before);
    }

    void idNotFoundFallsBackToSetCollector()
    {
        FakeLookup lookup;
        lookup.errors[0] = ScryfallIdentityLookupErrorKind::NotFound;
        const auto result = identity::IdentityResolver(lookup).resolve(workingCard());
        QCOMPARE(lookup.calls, QStringList({"id", "set-collector"}));
        QCOMPARE(lookup.set, QStringLiteral("m21"));
        QCOMPARE(lookup.collector, QStringLiteral("265"));
        QVERIFY(!lookup.language);
        verifyResolved(result, IdentityResolutionMethod::SetCollector);
    }

    void forwardsLanguage()
    {
        FakeLookup lookup;
        auto card = workingCard();
        card.identityHints.scryfallId.reset();
        card.identityHints.language = "pt";
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QCOMPARE(lookup.calls, QStringList({"set-collector"}));
        QVERIFY(lookup.language == QStringLiteral("pt"));
        verifyResolved(result, IdentityResolutionMethod::SetCollector);
    }

    void setCollectorNotFoundFallsBackToExactName()
    {
        FakeLookup lookup;
        lookup.errors[0] = ScryfallIdentityLookupErrorKind::NotFound;
        lookup.errors[1] = ScryfallIdentityLookupErrorKind::NotFound;
        auto card = workingCard();
        card.identityHints.name = "Éowyn, Shieldmaiden";
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QCOMPARE(lookup.calls, QStringList({"id", "set-collector", "exact-name"}));
        QCOMPARE(lookup.name, QString::fromUtf8("Éowyn, Shieldmaiden"));
        verifyResolved(result, IdentityResolutionMethod::Name);
    }

    void allNotFoundClearsStaleIdentityAndResolution()
    {
        FakeLookup lookup;
        lookup.errors.fill(ScryfallIdentityLookupErrorKind::NotFound);
        const auto original = workingCard();
        const auto result = identity::IdentityResolver(lookup).resolve(original);
        QCOMPARE(lookup.calls, QStringList({"id", "set-collector", "exact-name"}));
        QVERIFY(!result.identity);
        QCOMPARE(result.identityResolution.status, IdentityResolutionStatus::Unresolved);
        QVERIFY(result.identityResolution.query == original.identityHints.name);
        QVERIFY(!result.identityResolution.method);
        QVERIFY(!result.identityResolution.confidence);
        QVERIFY(result.identityResolution.candidates.empty());
        QVERIFY(!result.identityResolution.confirmed);
        verifyUnrelatedFields(result, original);
    }

    void realErrorsPropagateWithoutFallback_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<int>("kind");
        const std::array kinds{
            ScryfallIdentityLookupErrorKind::RateLimited,
            ScryfallIdentityLookupErrorKind::Server,
            ScryfallIdentityLookupErrorKind::Http,
            ScryfallIdentityLookupErrorKind::Network,
            ScryfallIdentityLookupErrorKind::Timeout,
            ScryfallIdentityLookupErrorKind::InvalidJson,
            ScryfallIdentityLookupErrorKind::InvalidPayload,
            ScryfallIdentityLookupErrorKind::InvalidContentType,
            ScryfallIdentityLookupErrorKind::ResponseTooLarge,
        };
        for (int stage = 0; stage < 3; ++stage) {
            for (const auto kind : kinds) {
                const QByteArray label = QByteArray::number(stage) + ':'
                    + QByteArray::number(static_cast<int>(kind));
                QTest::newRow(label.constData()) << stage << static_cast<int>(kind);
            }
        }
    }

    void realErrorsPropagateWithoutFallback()
    {
        QFETCH(int, stage);
        QFETCH(int, kind);
        FakeLookup lookup;
        for (int index = 0; index < stage; ++index) {
            lookup.errors[static_cast<std::size_t>(index)] =
                ScryfallIdentityLookupErrorKind::NotFound;
        }
        lookup.errors[static_cast<std::size_t>(stage)] =
            static_cast<ScryfallIdentityLookupErrorKind>(kind);
        const auto original = workingCard();
        const auto before = original;
        try {
            (void)identity::IdentityResolver(lookup).resolve(original);
            QFAIL("Expected the lookup error to propagate");
        } catch (const identity::ScryfallIdentityLookupError& error) {
            QCOMPARE(error.kind(), static_cast<ScryfallIdentityLookupErrorKind>(kind));
            QVERIFY(error.status() == 404); // Kind, not status, controls fallback.
            QCOMPARE(std::string(error.what()), std::string("lookup failed"));
        }
        const QStringList order{"id", "set-collector", "exact-name"};
        QCOMPARE(lookup.calls, order.mid(0, stage + 1));
        QVERIFY(original == before);
    }

    void unexpectedExceptionPropagates()
    {
        FakeLookup lookup;
        lookup.unexpectedError = true;
        QVERIFY_EXCEPTION_THROWN(
            identity::IdentityResolver(lookup).resolve(workingCard()),
            std::runtime_error
        );
        QCOMPARE(lookup.calls, QStringList({"id"}));
    }

    void confirmedCardIsUnchanged_data()
    {
        QTest::addColumn<bool>("hasIdentity");
        QTest::newRow("manual-identity") << true;
        QTest::newRow("confirmed-custom") << false;
    }

    void confirmedCardIsUnchanged()
    {
        QFETCH(bool, hasIdentity);
        FakeLookup lookup;
        auto card = workingCard();
        card.identityResolution.confirmed = true;
        if (!hasIdentity) {
            card.identity.reset();
            card.identityResolution.status = IdentityResolutionStatus::Custom;
        }
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QVERIFY(lookup.calls.empty());
        QVERIFY(result == card);
    }

    void customWithoutExplicitHint_data()
    {
        QTest::addColumn<QString>("entryKind");
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<QString>("origin");
        for (const auto& kind : {QStringLiteral("custom-card"), QStringLiteral("asset")}) {
            for (bool legacy : {false, true}) {
                for (const auto& origin : {QString(), QStringLiteral("filename")}) {
                    const QByteArray label = kind.toUtf8() + (legacy ? ":legacy:" : ":new:")
                        + origin.toUtf8();
                    QTest::newRow(label.constData()) << kind << legacy << origin;
                }
            }
        }
    }

    void customWithoutExplicitHint()
    {
        QFETCH(QString, entryKind);
        QFETCH(bool, legacy);
        QFETCH(QString, origin);
        FakeLookup lookup;
        auto card = workingCard();
        card.importSource.entryKind = entryKind.toStdString();
        if (!origin.isEmpty()) card.importSource.identityHintOrigin = origin.toStdString();
        if (!legacy) card.identity.reset();
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QVERIFY(lookup.calls.empty());
        if (legacy) {
            QVERIFY(result == card);
        } else {
            QVERIFY(!result.identity);
            QCOMPARE(result.identityResolution.status, IdentityResolutionStatus::Custom);
            QVERIFY(result.identityResolution.confirmed);
            QVERIFY(result.identityResolution.candidates.empty());
            QVERIFY(!result.identityResolution.method);
            QVERIFY(!result.identityResolution.query);
            QVERIFY(!result.identityResolution.confidence);
            verifyUnrelatedFields(result, card);
        }
    }

    void explicitCustomHintResolves_data()
    {
        QTest::addColumn<QString>("entryKind");
        QTest::newRow("custom-card") << QStringLiteral("custom-card");
        QTest::newRow("asset") << QStringLiteral("asset");
    }

    void explicitCustomHintResolves()
    {
        QFETCH(QString, entryKind);
        FakeLookup lookup;
        auto card = workingCard();
        card.identity.reset();
        card.identityHints = {.name = "Sol Ring"};
        card.identityResolution.status = IdentityResolutionStatus::Custom;
        card.importSource.entryKind = entryKind.toStdString();
        card.importSource.identityHintOrigin = "explicit-card-hint";
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QCOMPARE(lookup.calls, QStringList({"exact-name"}));
        QCOMPARE(lookup.name, QStringLiteral("Sol Ring"));
        verifyResolved(result, IdentityResolutionMethod::Name);
        verifyUnrelatedFields(result, card);
    }

    void absentEmptyAndIncompleteHints_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("absent") << 0;
        QTest::newRow("empty") << 1;
        QTest::newRow("set-only") << 2;
        QTest::newRow("collector-only") << 3;
        QTest::newRow("explicit-custom-miss") << 4;
    }

    void absentEmptyAndIncompleteHints()
    {
        QFETCH(int, variant);
        FakeLookup lookup;
        auto card = workingCard();
        card.identityHints = {};
        if (variant == 1) card.identityHints = {
            .name = "", .setCode = "", .collectorNumber = "", .scryfallId = "",
        };
        if (variant == 2) card.identityHints.setCode = "m21";
        if (variant == 3) card.identityHints.collectorNumber = "265";
        if (variant == 4) {
            card.importSource.entryKind = "custom-card";
            card.importSource.identityHintOrigin = "explicit-card-hint";
            card.identityResolution.status = IdentityResolutionStatus::Custom;
        }
        const auto result = identity::IdentityResolver(lookup).resolve(card);
        QVERIFY(lookup.calls.empty());
        QVERIFY(!result.identity);
        QCOMPARE(result.identityResolution.status, IdentityResolutionStatus::Unresolved);
        QVERIFY(!result.identityResolution.confirmed);
        QVERIFY(result.identityResolution.candidates.empty());
        QVERIFY(!result.identityResolution.method);
        QVERIFY(!result.identityResolution.confidence);
        QVERIFY(result.identityResolution.query == card.identityHints.name);
        verifyUnrelatedFields(result, card);
    }

    void stableIdentity_data()
    {
        QTest::addColumn<bool>("hasOracle");
        QTest::addColumn<QString>("expectedId");
        QTest::newRow("oracle") << true
            << QStringLiteral("scryfall:oracle:bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
        QTest::newRow("printing") << false
            << QStringLiteral("scryfall:card:aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    }

    void stableIdentity()
    {
        QFETCH(bool, hasOracle);
        QFETCH(QString, expectedId);
        FakeLookup lookup;
        if (!hasOracle) lookup.found.oracleId.reset();
        const auto result = identity::IdentityResolver(lookup).resolve(workingCard());
        QVERIFY(result.identity.has_value());
        QCOMPARE(QString::fromStdString(result.identity->id), expectedId);
        QVERIFY(result.identity->metadata.has_value());
        QCOMPARE(result.identity->metadata->faces.size(), std::size_t{1});
        QCOMPARE(result.identity->metadata->faces.front().name, std::string("Sol Ring"));
    }
};

QTEST_GUILESS_MAIN(IdentityResolverTest)
#include "IdentityResolverTest.moc"
