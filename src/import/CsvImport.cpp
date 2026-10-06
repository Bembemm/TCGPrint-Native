#include "import/CsvImport.h"

#include "import/ImportFailure.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>

#include <array>
#include <limits>

namespace tcgprint::imports {
namespace {

struct CsvRecord {
    QStringList fields;
    int line{1};
};

struct ResolvedColumns {
    std::optional<int> name;
    std::optional<int> quantity;
    std::optional<int> setCode;
    std::optional<int> collectorNumber;
    std::optional<int> scryfallId;
    std::optional<int> imageUrl;
    std::optional<int> language;
};

QString decodeText(const ImportSource& source)
{
    if (source.originalText.has_value()) {
        return *source.originalText;
    }
    if (!source.originalBytes.has_value()) {
        throw ImportFailureError(
            QStringLiteral(
                "CSV/TSV import requires original text or bytes."
            ),
            QStringLiteral("UNSUPPORTED_INPUT"),
            source.id,
            source.sourcePath
        );
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(*source.originalBytes);
    if (decoder.hasError()) {
        throw ImportFailureError(
            QStringLiteral("CSV/TSV must be valid UTF-8."),
            QStringLiteral("INVALID_CSV"),
            source.id,
            source.sourcePath
        );
    }
    return text;
}

std::vector<CsvRecord> parseRecords(
    const QString& text,
    QChar delimiter,
    const ImportSource& source
)
{
    std::vector<CsvRecord> records;
    QStringList fields;
    QString field;
    bool inQuotes = false;
    bool quoteClosed = false;
    bool sawAnyCharacter = false;
    int line = 1;
    int recordLine = 1;

    const auto fail = [&source]() -> void {
        throw ImportFailureError(
            QStringLiteral("CSV/TSV parser rejected the input."),
            QStringLiteral("INVALID_CSV"),
            source.id,
            source.sourcePath
        );
    };

    const auto finishRecord = [&]() {
        fields.push_back(field);
        field.clear();
        quoteClosed = false;

        bool nonEmpty = false;
        for (const QString& value : fields) {
            if (!value.isEmpty()) {
                nonEmpty = true;
                break;
            }
        }
        if (nonEmpty || fields.size() > 1 || sawAnyCharacter) {
            records.push_back(CsvRecord{
                .fields = fields,
                .line = recordLine,
            });
        }
        fields.clear();
        sawAnyCharacter = false;
    };

    for (qsizetype index = 0; index < text.size(); ++index) {
        const QChar ch = text[index];

        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (
                    index + 1 < text.size()
                    && text[index + 1] == QLatin1Char('"')
                ) {
                    field.append(QLatin1Char('"'));
                    ++index;
                } else {
                    inQuotes = false;
                    quoteClosed = true;
                }
                continue;
            }

            field.append(ch);
            if (ch == QLatin1Char('\n')) {
                ++line;
            } else if (ch == QLatin1Char('\r')) {
                if (
                    index + 1 < text.size()
                    && text[index + 1] == QLatin1Char('\n')
                ) {
                    field.append(text[index + 1]);
                    ++index;
                }
                ++line;
            }
            continue;
        }

        if (quoteClosed) {
            if (ch == delimiter) {
                fields.push_back(field);
                field.clear();
                quoteClosed = false;
                sawAnyCharacter = true;
                continue;
            }
            if (
                ch != QLatin1Char('\n')
                && ch != QLatin1Char('\r')
            ) {
                fail();
            }
        }

        if (ch == QLatin1Char('"')) {
            if (!field.isEmpty()) {
                fail();
            }
            inQuotes = true;
            sawAnyCharacter = true;
            continue;
        }

        if (ch == delimiter) {
            fields.push_back(field);
            field.clear();
            sawAnyCharacter = true;
            continue;
        }

        if (
            ch == QLatin1Char('\n')
            || ch == QLatin1Char('\r')
        ) {
            finishRecord();
            if (
                ch == QLatin1Char('\r')
                && index + 1 < text.size()
                && text[index + 1] == QLatin1Char('\n')
            ) {
                ++index;
            }
            ++line;
            recordLine = line;
            continue;
        }

        field.append(ch);
        sawAnyCharacter = true;
    }

    if (inQuotes) {
        fail();
    }

    if (!field.isEmpty() || !fields.isEmpty() || sawAnyCharacter) {
        finishRecord();
    }

    return records;
}

QString normalizedHeader(const QString& header)
{
    QString normalized = header.trimmed().toLower();
    normalized.remove(
        QRegularExpression(QStringLiteral("[^a-z0-9]"))
    );
    return normalized;
}

const std::array<QStringList, 7>& aliases()
{
    static const std::array<QStringList, 7> value{
        QStringList{QStringLiteral("name"), QStringLiteral("cardname")},
        QStringList{
            QStringLiteral("quantity"),
            QStringLiteral("count"),
            QStringLiteral("qty"),
            QStringLiteral("copies")
        },
        QStringList{QStringLiteral("set"), QStringLiteral("setcode")},
        QStringList{
            QStringLiteral("collectornumber"),
            QStringLiteral("collector"),
            QStringLiteral("number")
        },
        QStringList{QStringLiteral("scryfallid")},
        QStringList{
            QStringLiteral("imageurl"),
            QStringLiteral("image"),
            QStringLiteral("imageuri")
        },
        QStringList{QStringLiteral("language"), QStringLiteral("lang")},
    };
    return value;
}

std::optional<int> resolveColumn(
    const QStringList& headers,
    const std::optional<CsvColumnSelector>& selector,
    const QStringList& fieldAliases,
    const QString& fieldName
)
{
    if (selector.has_value()) {
        if (const int* index = std::get_if<int>(&*selector)) {
            if (*index < 0 || *index >= headers.size()) {
                throw ImportFailureError(
                    QStringLiteral(
                        "CSV mapping refers to a missing column."
                    ),
                    QStringLiteral("MAPPING_INVALID")
                );
            }
            return *index;
        }

        const QString requested =
            std::get<QString>(*selector);
        const int index = headers.indexOf(requested);
        if (index < 0) {
            throw ImportFailureError(
                QStringLiteral(
                    "CSV mapping refers to a missing header."
                ),
                QStringLiteral("MAPPING_INVALID")
            );
        }
        return index;
    }

    for (int index = 0; index < headers.size(); ++index) {
        if (
            fieldAliases.contains(
                normalizedHeader(headers[index])
            )
        ) {
            return index;
        }
    }

    Q_UNUSED(fieldName);
    return std::nullopt;
}

ResolvedColumns resolveColumns(
    const QStringList& headers,
    const std::optional<CsvImportMapping>& mapping
)
{
    const CsvImportMapping empty;
    const CsvImportMapping& config =
        mapping.has_value() ? *mapping : empty;
    const auto& names = aliases();

    return ResolvedColumns{
        .name = resolveColumn(
            headers,
            config.name,
            names[0],
            QStringLiteral("name")
        ),
        .quantity = resolveColumn(
            headers,
            config.quantity,
            names[1],
            QStringLiteral("quantity")
        ),
        .setCode = resolveColumn(
            headers,
            config.setCode,
            names[2],
            QStringLiteral("setCode")
        ),
        .collectorNumber = resolveColumn(
            headers,
            config.collectorNumber,
            names[3],
            QStringLiteral("collectorNumber")
        ),
        .scryfallId = resolveColumn(
            headers,
            config.scryfallId,
            names[4],
            QStringLiteral("scryfallId")
        ),
        .imageUrl = resolveColumn(
            headers,
            config.imageUrl,
            names[5],
            QStringLiteral("imageUrl")
        ),
        .language = resolveColumn(
            headers,
            config.language,
            names[6],
            QStringLiteral("language")
        ),
    };
}

std::optional<QString> selectedValue(
    const QStringList& record,
    const std::optional<int>& index
)
{
    if (!index.has_value() || *index >= record.size()) {
        return std::nullopt;
    }
    const QString value = record[*index].trimmed();
    return value.isEmpty()
        ? std::nullopt
        : std::optional<QString>(value);
}

void addMappingField(
    QJsonObject& fields,
    const QString& name,
    const std::optional<int>& index,
    const QStringList& headers
)
{
    if (index.has_value()) {
        fields.insert(name, headers[*index]);
    }
}

} // namespace

ImporterOutput parseCsvImport(
    const ImportSource& source,
    const std::optional<CsvImportMapping>& mapping,
    std::optional<QChar> delimiter,
    const ImportLimitOverrides& limitOverrides
)
{
    const ImportLimits limits =
        resolveImportLimits(limitOverrides);
    QString text = decodeText(source);
    if (!text.isEmpty() && text.front() == QChar(0xfeff)) {
        text.remove(0, 1);
    }

    const qsizetype byteLength = text.toUtf8().size();
    if (
        byteLength < 0
        || static_cast<std::uint64_t>(byteLength)
            > limits.maxCsvBytes
    ) {
        throw ImportFailureError(
            QStringLiteral("CSV/TSV exceeds the configured byte limit."),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    const QChar selectedDelimiter =
        delimiter.value_or(
            source.filename.has_value()
                && source.filename->toLower().endsWith(
                    QStringLiteral(".tsv")
                )
            ? QLatin1Char('\t')
            : QLatin1Char(',')
        );

    std::vector<CsvRecord> records =
        parseRecords(text, selectedDelimiter, source);

    if (records.empty() || records.front().fields.isEmpty()) {
        ImporterOutput output;
        output.warnings.push_back(ImportWarning{
            .code = QStringLiteral("MISSING_HEADER"),
            .message = QStringLiteral("CSV/TSV has no header row."),
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
        });
        return output;
    }

    QStringList headers = records.front().fields;
    records.erase(records.begin());
    for (QString& header : headers) {
        header = header.trimmed();
    }

    const ResolvedColumns columns =
        resolveColumns(headers, mapping);

    QJsonObject mappingFields;
    addMappingField(
        mappingFields,
        QStringLiteral("name"),
        columns.name,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("quantity"),
        columns.quantity,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("setCode"),
        columns.setCode,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("collectorNumber"),
        columns.collectorNumber,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("scryfallId"),
        columns.scryfallId,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("imageUrl"),
        columns.imageUrl,
        headers
    );
    addMappingField(
        mappingFields,
        QStringLiteral("language"),
        columns.language,
        headers
    );

    QSet<int> mappedIndexes;
    const std::array<std::optional<int>, 7> indexes{
        columns.name,
        columns.quantity,
        columns.setCode,
        columns.collectorNumber,
        columns.scryfallId,
        columns.imageUrl,
        columns.language,
    };
    for (const auto& index : indexes) {
        if (index.has_value()) {
            mappedIndexes.insert(*index);
        }
    }

    QStringList unknownFields;
    for (int index = 0; index < headers.size(); ++index) {
        if (!mappedIndexes.contains(index)) {
            unknownFields.push_back(headers[index]);
        }
    }

    ImporterOutput output;
    output.mappings.push_back(ImportMapping{
        .sourceId = source.id,
        .format =
            selectedDelimiter == QLatin1Char('\t')
            ? QStringLiteral("tsv")
            : QStringLiteral("csv"),
        .fields = mappingFields,
        .unknownFields = unknownFields,
    });

    if (!columns.name.has_value()) {
        output.warnings.push_back(ImportWarning{
            .code = QStringLiteral("MISSING_NAME_COLUMN"),
            .message = QStringLiteral(
                "No card name column was recognized."
            ),
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
        });
    }

    if (
        static_cast<std::uint64_t>(records.size())
        > limits.maxCsvRows
    ) {
        throw ImportFailureError(
            QStringLiteral("CSV/TSV exceeds the configured row limit."),
            QStringLiteral("INPUT_TOO_LARGE"),
            source.id,
            source.sourcePath
        );
    }

    for (
        std::size_t index = 0;
        index < records.size();
        ++index
    ) {
        const CsvRecord& parsedRecord = records[index];
        const QStringList& record = parsedRecord.fields;

        if (record.size() != headers.size()) {
            output.warnings.push_back(ImportWarning{
                .code = QStringLiteral("ROW_WIDTH_MISMATCH"),
                .message = QStringLiteral(
                    "CSV/TSV row width does not match the header."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
                .line = parsedRecord.line,
            });
            continue;
        }

        const auto name =
            selectedValue(record, columns.name);
        if (!name.has_value()) {
            output.warnings.push_back(ImportWarning{
                .code = QStringLiteral("MISSING_NAME"),
                .message = QStringLiteral(
                    "CSV/TSV row has no mapped card name."
                ),
                .sourceId = source.id,
                .sourceFilename = source.filename,
                .sourcePath = source.sourcePath,
                .line = parsedRecord.line,
            });
            continue;
        }

        const auto rawQuantity =
            selectedValue(record, columns.quantity);
        std::uint64_t quantity = 1;
        if (rawQuantity.has_value()) {
            bool ok = false;
            quantity = rawQuantity->toULongLong(&ok);
            constexpr std::uint64_t MaxSafeInteger =
                9'007'199'254'740'991ULL;
            if (
                !ok
                || quantity == 0
                || quantity > MaxSafeInteger
            ) {
                output.warnings.push_back(ImportWarning{
                    .code = QStringLiteral("INVALID_QUANTITY"),
                    .message = QStringLiteral(
                        "Quantity is not a positive safe integer."
                    ),
                    .sourceId = source.id,
                    .sourceFilename = source.filename,
                    .sourcePath = source.sourcePath,
                    .line = parsedRecord.line,
                    .field = QStringLiteral("quantity"),
                });
                continue;
            }
        }

        QJsonObject rawRecord;
        for (int column = 0; column < headers.size(); ++column) {
            rawRecord.insert(
                headers[column],
                record[column]
            );
        }

        ImportedCardHint hint{
            .name = name,
            .setCode =
                selectedValue(record, columns.setCode),
            .collectorNumber =
                selectedValue(record, columns.collectorNumber),
            .scryfallId =
                selectedValue(record, columns.scryfallId),
            .imageUrl =
                selectedValue(record, columns.imageUrl),
            .language =
                selectedValue(record, columns.language),
        };

        output.entries.push_back(ImportedEntry{
            .id = QStringLiteral("%1:row:%2")
                .arg(
                    source.id,
                    QString::number(parsedRecord.line)
                ),
            .kind = ImportedEntryKind::DeckCard,
            .order =
                source.order
                + static_cast<int>(output.entries.size()),
            .quantity = quantity,
            .sourceId = source.id,
            .sourceFilename = source.filename,
            .sourcePath = source.sourcePath,
            .cardHint = std::move(hint),
            .metadata = QJsonObject{
                {
                    QStringLiteral("rawRecord"),
                    rawRecord
                },
                {
                    QStringLiteral("row"),
                    parsedRecord.line
                },
            },
        });
    }

    return output;
}

} // namespace tcgprint::imports
