#pragma once

#include "import/ImportTypes.h"

namespace tcgprint::imports {

struct DetectionPolicy {
    double autoSelectMinimum;
    double ambiguousMinimum;
    double ambiguityMargin;
    double extensionAdjustment;
};

inline constexpr DetectionPolicy DetectionPolicyDefault{
    .autoSelectMinimum = 0.78,
    .ambiguousMinimum = 0.50,
    .ambiguityMargin = 0.12,
    .extensionAdjustment = 0.04,
};

ImportDetection detectImport(
    const ImportDetectionInput& input,
    const DetectionPolicy& policy = DetectionPolicyDefault
);

} // namespace tcgprint::imports
