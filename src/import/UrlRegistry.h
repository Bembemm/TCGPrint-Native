#pragma once

#include <QString>

namespace tcgprint::imports {

enum class UrlResolutionKind {
    Adapter,
    KnownUnsupported,
    DirectFile,
};

struct UrlAdapterResolution {
    UrlResolutionKind kind{UrlResolutionKind::DirectFile};
    QString adapterId;
    QString siteId;
    QString message;
};

UrlAdapterResolution resolveUrlAdapter(const QString& value);

} // namespace tcgprint::imports
