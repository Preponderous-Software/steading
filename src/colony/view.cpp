#include "view.h"

#include <cstdio>

namespace steading {

namespace {
float centre(int32_t cell) { return static_cast<float>(cell) + 0.5f; }
}  // namespace

const char* prefabFor(BuildingKind k) {
    switch (k) {
        case BuildingKind::Stockpile: return "/Prefabs/Stockpile";
        case BuildingKind::Hut: return "/Prefabs/Hut";
        case BuildingKind::HutPlan: return "/Prefabs/HutPlan";
    }
    return "/Prefabs/Unknown";
}

std::string primName(const char* prefix, int64_t id) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%s%06lld", prefix, static_cast<long long>(id));
    return buf;
}

std::vector<ViewItem> view(const Colony& c) {
    std::vector<ViewItem> v;
    const Settlers& S = c.settlers;
    v.reserve(S.size() + c.buildings.size());
    for (size_t i = 0; i < S.size(); ++i)
        v.push_back({"Settlers/" + primName("S_", S.id[i]), "/Prefabs/Settler", centre(S.x[i]), centre(S.y[i]), 0.f,
                     1.f});
    const Buildings& B = c.buildings;
    for (size_t i = 0; i < B.size(); ++i)
        v.push_back({"Buildings/" + primName("B_", B.id[i]), prefabFor(static_cast<BuildingKind>(B.kind[i])),
                     centre(B.x[i]), centre(B.y[i]), 0.f, 1.f});
    const Grid& g = c.grid;
    for (int32_t y = 0; y < g.h; ++y)
        for (int32_t x = 0; x < g.w; ++x) {
            const size_t k = g.at(x, y);
            if (g.resType[k] != static_cast<int32_t>(Resource::Tree) || g.resAmount[k] == 0) continue;
            char n[40];
            std::snprintf(n, sizeof n, "Trees/T_%03d_%03d", x, y);
            v.push_back({n, "/Prefabs/Tree", centre(x), centre(y), 0.f, 0.5f + 0.05f * static_cast<float>(g.resAmount[k])});
        }
    return v;
}

}  // namespace steading
