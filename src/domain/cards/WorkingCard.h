#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tcgprint::cards {

enum class CardFaceSide
{
    Front,
    Back,
};

enum class ArtworkSource
{
    Scryfall,
    Upload,
    Mpc,
    Url,
    Custom,
};

enum class IdentityResolutionMethod
{
    ScryfallId,
    SetCollector,
    Name,
    Filename,
    Ocr,
    Fuzzy,
    Manual,
    Custom,
};

enum class IdentityResolutionStatus
{
    Resolved,
    Suggested,
    Ambiguous,
    Unresolved,
    Custom,
};

using IdentityMetadataScalar = std::variant<std::string, bool>;

struct IdentityMetadataFace final
{
    std::string name;

    bool operator==(const IdentityMetadataFace&) const = default;
};

struct IdentityMetadataRelatedCard final
{
    std::string id;
    std::string name;
    std::string component;
    std::optional<std::string> typeLine;

    bool operator==(const IdentityMetadataRelatedCard&) const = default;
};

struct CardIdentityMetadata final
{
    std::map<std::string, IdentityMetadataScalar> scalars;
    std::vector<IdentityMetadataFace> faces;
    std::vector<IdentityMetadataRelatedCard> relatedCards;

    bool operator==(const CardIdentityMetadata&) const = default;
};

struct CardIdentity final
{
    // provider/id remain first for source-compatible aggregate initializers.
    std::string provider;
    std::string id;
    std::string name;
    std::optional<std::string> scryfallId;
    std::optional<std::string> oracleId;
    std::optional<std::string> setCode;
    std::optional<std::string> collectorNumber;
    std::optional<std::string> lang;
    IdentityResolutionMethod resolutionMethod{
        IdentityResolutionMethod::Manual
    };
    double confidence{0.0};
    std::optional<CardIdentityMetadata> metadata;

    bool operator==(const CardIdentity&) const = default;
};

struct WorkingCardImportSource final
{
    std::string sourceId;
    std::optional<std::string> filename;
    std::string importKind;
    std::string entryKind;
    std::optional<std::string> identityHintOrigin;

    bool operator==(const WorkingCardImportSource&) const = default;
};

struct CardIdentityHints final
{
    std::optional<std::string> name;
    std::optional<std::string> setCode;
    std::optional<std::string> collectorNumber;
    std::optional<std::string> scryfallId;
    std::optional<std::string> language;

    bool operator==(const CardIdentityHints&) const = default;
};

struct CardFace final
{
    std::string id;
    CardFaceSide side{CardFaceSide::Front};
    std::optional<std::string> name;
    std::optional<std::string> importedAssetId;
    std::vector<std::string> slots;

    bool operator==(const CardFace&) const = default;
};

struct SelectedArtwork final
{
    std::string candidateId;
    ArtworkSource source{ArtworkSource::Custom};
    std::optional<std::string> identityId;
    CardFaceSide faceId{CardFaceSide::Front};
    std::optional<std::string> providerAssetId;
    std::optional<std::string> selectedArtworkId;
    std::optional<std::string> selectionPolicy;

    bool operator==(const SelectedArtwork&) const = default;
};

struct IdentityResolutionCandidate final
{
    CardIdentity identity;
    double score{0.0};
    std::string reason;

    bool operator==(const IdentityResolutionCandidate&) const = default;
};

struct IdentityResolution final
{
    IdentityResolutionStatus status{
        IdentityResolutionStatus::Unresolved
    };
    std::optional<IdentityResolutionMethod> method;
    std::optional<std::string> query;
    std::optional<double> confidence;
    std::vector<IdentityResolutionCandidate> candidates;
    bool confirmed{false};

    bool operator==(const IdentityResolution&) const = default;
};

struct BackLibraryAssetReference final
{
    std::string assetId;
    std::string sha256;
    std::string format;

    bool operator==(const BackLibraryAssetReference&) const = default;
};

enum class MpcReferenceOrigin
{
    OrderImport,
    GallerySelection,
};

enum class MpcProviderCardType
{
    Card,
    Cardback,
};

struct WorkingCardMpcReference final
{
    CardFaceSide faceId{CardFaceSide::Front};
    std::string importedAssetId;
    std::optional<std::string> providerAssetId;
    std::optional<std::string> selectedArtworkId;
    std::optional<MpcReferenceOrigin> referenceOrigin;
    std::optional<MpcProviderCardType> providerCardType;
    std::vector<std::string> slots;
    bool availableLocally{false};

    bool operator==(const WorkingCardMpcReference&) const = default;
};

struct SharedMpcCardbackProvenance final
{
    std::string sourceId;
    std::optional<std::string> sourceFilename;

    bool operator==(const SharedMpcCardbackProvenance&) const = default;
};

struct WorkingCardSharedMpcCardback final
{
    std::string importedAssetId;
    std::optional<std::string> providerAssetId;
    std::optional<std::string> selectedArtworkId;
    std::string originalFormat;
    bool availableLocally{false};
    SharedMpcCardbackProvenance provenance;

    bool operator==(const WorkingCardSharedMpcCardback&) const = default;
};

struct WorkingCardFaceAssociation final
{
    std::string slot;
    std::optional<std::string> frontAssetId;
    std::optional<std::string> backAssetId;
    std::optional<double> confidence;
    std::optional<std::string> reason;
    std::optional<bool> accepted;

    bool operator==(const WorkingCardFaceAssociation&) const = default;
};

enum class BackMode
{
    Auto,
    ProjectDefault,
    Manual,
    None,
};

enum class BackModeSelectionPolicy
{
    Automatic,
    Explicit,
};

struct WorkingCard final
{
    std::string id;
    std::string displayName;
    std::uint32_t quantity{1};
    int order{0};
    std::optional<CardIdentity> identity;
    BackMode backMode{BackMode::Auto};
    BackModeSelectionPolicy backModeSelectionPolicy{
        BackModeSelectionPolicy::Automatic
    };

    // Durable Project fields. Defaults preserve lightweight editor/test use.
    std::optional<std::string> section;
    WorkingCardImportSource importSource;
    CardIdentityHints identityHints;
    IdentityResolution identityResolution;
    std::vector<CardFace> faces;
    std::optional<SelectedArtwork> selectedFrontArtwork;
    std::optional<SelectedArtwork> selectedBackArtwork;
    std::optional<BackLibraryAssetReference> manualBackAsset;
    std::optional<SelectedArtwork> manualBackArtwork;
    std::vector<std::string> localArtworkIds;
    std::vector<WorkingCardMpcReference> mpcReferences;
    std::optional<WorkingCardSharedMpcCardback> sharedMpcCardback;
    std::vector<WorkingCardFaceAssociation> faceAssociations;

    bool operator==(const WorkingCard&) const = default;
};

[[nodiscard]] inline bool isValidWorkingCard(const WorkingCard& card) noexcept
{
    return !card.id.empty() && card.quantity > 0;
}

} // namespace tcgprint::cards
