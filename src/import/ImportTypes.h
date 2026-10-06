#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace tcgprint::imports {

enum class ImportKind {
    Image,
    Svg,
    SimpleDecklist,
    ArenaLike,
    MtgoLike,
    XmageLike,
    MwdeckLike,
    Csv,
    Tsv,
    Json,
    GenericXml,
    MpcAutofillXml,
    Zip,
    Url,
    Unknown,
};

enum class ImportDetectionStatus {
    AutoSelected,
    UserSelected,
    Ambiguous,
    Unknown,
};

struct ImportCandidate {
    ImportKind kind{ImportKind::Unknown};
    double confidence{0.0};
    QStringList reasons;
    std::optional<QString> originalFormat;
};

struct ImportDetection {
    ImportDetectionStatus status{ImportDetectionStatus::Unknown};
    std::vector<ImportCandidate> candidates;
    std::optional<ImportCandidate> selected;
    QStringList reasons;
};

struct ImportDetectionInput {
    std::optional<QByteArray> bytes;
    std::optional<QString> text;
    std::optional<QString> fileName;
    std::optional<QString> mediaType;
    std::optional<QString> sourceUrl;
    std::optional<QString> adapterId;
    bool urlLike{false};
};

QString importKindName(ImportKind kind);

} // namespace tcgprint::imports
