#pragma once

#include "import/ImportLimits.h"
#include "import/ImportTypes.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

namespace tcgprint::imports {

struct SafeXmlNode {
    QString name;
    QJsonObject attributes;
    QString text;
    std::vector<SafeXmlNode> children;
};

struct SafeXmlDocument {
    SafeXmlNode root;
    std::uint64_t nodeCount{0};
    std::uint64_t maxDepth{0};
};

SafeXmlDocument parseSafeXml(
    const QByteArray& input,
    const ImportLimitOverrides& limitOverrides = {}
);

SafeXmlDocument parseSafeXml(
    const QString& input,
    const ImportLimitOverrides& limitOverrides = {}
);

ImporterOutput importGenericXml(
    const ImportSource& source,
    const ImportLimitOverrides& limitOverrides = {}
);

} // namespace tcgprint::imports
