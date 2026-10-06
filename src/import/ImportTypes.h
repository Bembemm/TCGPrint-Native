#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>

#include <optional>
#include <variant>
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

enum class ImportSourceKind {
    File,
    FolderFile,
    Text,
    Clipboard,
    ZipEntry,
    Url,
};

struct ImportSource {
    QString id;
    ImportSourceKind kind{ImportSourceKind::Text};
    std::optional<QString> filename;
    std::optional<QString> sourcePath;
    std::optional<QString> parentSourceId;
    int order{0};
    std::optional<QString> originalFormat;
    std::optional<QString> mediaType;
    std::optional<QString> sourceUrl;
    std::optional<QString> adapterId;
    std::uint64_t sizeBytes{0};
    std::optional<QByteArray> originalBytes;
    std::optional<QString> originalText;
    std::optional<QString> sha256;
    QJsonObject metadata;
};

struct ImportedCardHint {
    std::optional<QString> name;
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    std::optional<QString> scryfallId;
    std::optional<QString> imageUrl;
    std::optional<QString> language;
    std::optional<QString> section;
};

enum class ImportedEntryKind {
    DeckCard,
    CustomCard,
    Asset,
    MpcOrderCard,
    Document,
};

struct ImportedEntry {
    QString id;
    ImportedEntryKind kind{ImportedEntryKind::DeckCard};
    int order{0};
    std::uint64_t quantity{1};
    QString sourceId;
    std::optional<QString> sourceFilename;
    std::optional<QString> sourcePath;
    std::optional<ImportedCardHint> cardHint;
    std::optional<QString> nameSuggestion;
    std::optional<QString> section;
    QJsonObject metadata;
};

struct ImportWarning {
    QString code;
    QString message;
    std::optional<QString> sourceId;
    std::optional<QString> sourceFilename;
    std::optional<QString> sourcePath;
    std::optional<int> line;
    std::optional<QString> field;
};

using CsvColumnSelector = std::variant<int, QString>;

struct CsvImportMapping {
    std::optional<CsvColumnSelector> name;
    std::optional<CsvColumnSelector> quantity;
    std::optional<CsvColumnSelector> setCode;
    std::optional<CsvColumnSelector> collectorNumber;
    std::optional<CsvColumnSelector> scryfallId;
    std::optional<CsvColumnSelector> imageUrl;
    std::optional<CsvColumnSelector> language;
};

struct JsonImportMapping {
    std::optional<QString> collectionPath;
    std::optional<QString> name;
    std::optional<QString> quantity;
    std::optional<QString> setCode;
    std::optional<QString> collectorNumber;
    std::optional<QString> scryfallId;
    std::optional<QString> imageUrl;
    std::optional<QString> language;
    std::optional<QString> section;
};

struct ImportMapping {
    QString sourceId;
    QString format;
    QJsonObject fields;
    QStringList unknownFields;
};

struct ImporterOutput {
    std::vector<ImportedEntry> entries;
    std::vector<ImportWarning> warnings;
    std::vector<ImportMapping> mappings;
};

QString importKindName(ImportKind kind);

} // namespace tcgprint::imports
