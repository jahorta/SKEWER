#pragma once

#include "Diagnostics.h"
#include "FieldDiscovery.h"
#include "SceneModel.h"

#include "SPICE/SpiceEct/EctModel.h"
#include "SPICE/SpiceMLD/Model/MldFile.h"

#include <vector>

namespace skewer::core {

struct FieldDocument {
    FieldAssetPair assets{};
    spice::mld::model::MldFile mld{};
    spice::ect::EctFile ect{};
    SceneModel scene{};
    std::vector<Diagnostic> diagnostics{};
};

} // namespace skewer::core
