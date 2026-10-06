#pragma once

#include <QString>

#include <optional>
#include <stdexcept>

namespace tcgprint::imports {

class ImportFailureError final : public std::runtime_error
{
public:
    ImportFailureError(
        const QString& message,
        QString code,
        std::optional<QString> sourceId = std::nullopt,
        std::optional<QString> sourcePath = std::nullopt
    )
        : std::runtime_error(message.toUtf8().constData())
        , code_(std::move(code))
        , sourceId_(std::move(sourceId))
        , sourcePath_(std::move(sourcePath))
    {
    }

    const QString& code() const noexcept
    {
        return code_;
    }

    const std::optional<QString>& sourceId() const noexcept
    {
        return sourceId_;
    }

    const std::optional<QString>& sourcePath() const noexcept
    {
        return sourcePath_;
    }

private:
    QString code_;
    std::optional<QString> sourceId_;
    std::optional<QString> sourcePath_;
};

} // namespace tcgprint::imports
