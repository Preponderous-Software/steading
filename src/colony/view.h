// The game's projection of runtime state into things a viewer draws. Per RFC
// 0019 the renderer (Strata) and the history recorder consume view items and
// USD prefabs; they never read saves or components directly.
#pragma once

#include <string>
#include <vector>

#include "components.h"

namespace steading {

struct ViewItem {
    std::string path;    // stable per thing, e.g. "Settlers/S_000042"
    std::string prefab;  // USD prefab path, e.g. "/Prefabs/Settler"
    float x, y, z, scale;
};

std::vector<ViewItem> view(const Colony& c);

// Prefab path for a building kind ("/Prefabs/Hut", ...).
const char* prefabFor(BuildingKind k);
// Zero-padded prim name: "S_000042".
std::string primName(const char* prefix, int64_t id);

}  // namespace steading
