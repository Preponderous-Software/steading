#include "generate.h"

#include <stdexcept>

#include "rng.h"
#include "stats.h"

namespace steading {

Colony generate(const GenParams& p) {
    if (p.width < 8 || p.height < 8) throw std::invalid_argument("map must be at least 8x8");
    if (p.settlers < 0) throw std::invalid_argument("settlers must be >= 0");
    Colony c;
    Grid& g = c.grid;
    g.init(p.width, p.height);
    Rng rng{mix64(p.seed)};
    for (int32_t y = 0; y < g.h; ++y)
        for (int32_t x = 0; x < g.w; ++x) {
            const size_t i = g.at(x, y);
            const double elevation =
                0.65 * valueNoise(p.seed, x / 12.0, y / 12.0) + 0.35 * valueNoise(p.seed + 1, x / 5.0, y / 5.0);
            const double forest = valueNoise(p.seed + 7, x / 7.0, y / 7.0);
            const int roll = rng.below(1000);
            if (elevation < 0.30) {
                g.terrain[i] = static_cast<int32_t>(Terrain::Water);
            } else if (elevation > 0.72) {
                g.terrain[i] = static_cast<int32_t>(Terrain::Rock);
                if (roll < 300) {
                    g.resType[i] = static_cast<int32_t>(Resource::Stone);
                    g.resAmount[i] = Rules::kStoneAmount;
                }
            } else {
                g.terrain[i] = static_cast<int32_t>(Terrain::Grass);
                if (forest > 0.55 && roll < 450) {
                    g.resType[i] = static_cast<int32_t>(Resource::Tree);
                    g.resAmount[i] = Rules::kTreeAmount;
                } else if (roll < 25) {
                    g.resType[i] = static_cast<int32_t>(Resource::Berry);
                    g.resAmount[i] = Rules::kBerryAmount;
                }
            }
        }

    // Stockpile: the clear grass cell nearest the centre (ring search).
    const int32_t cx = g.w / 2, cy = g.h / 2;
    int32_t sx = -1, sy = -1;
    for (int32_t rad = 0; rad < g.w + g.h && sx < 0; ++rad)
        for (int32_t dy = -rad; dy <= rad && sx < 0; ++dy)
            for (int32_t dx = -rad; dx <= rad && sx < 0; ++dx) {
                const int32_t x = cx + dx, y = cy + dy;
                if (!g.inBounds(x, y)) continue;
                const size_t i = g.at(x, y);
                if (g.terrain[i] == static_cast<int32_t>(Terrain::Grass) &&
                    g.resType[i] == static_cast<int32_t>(Resource::None)) {
                    sx = x;
                    sy = y;
                }
            }
    if (sx < 0) {  // pathological map: clear the centre tile and use it
        sx = cx;
        sy = cy;
        g.terrain[g.at(sx, sy)] = static_cast<int32_t>(Terrain::Grass);
        g.resType[g.at(sx, sy)] = static_cast<int32_t>(Resource::None);
        g.resAmount[g.at(sx, sy)] = 0;
    }

    Globals& G = c.globals;
    G.seed = static_cast<int64_t>(p.seed);
    G.rng = static_cast<int64_t>(mix64(p.seed ^ 0xC0103ULL));
    G.food = 20;
    G.stockX = sx;
    G.stockY = sy;
    G.maxSettlers = p.maxSettlers > 0 ? p.maxSettlers : 2 * static_cast<int64_t>(p.settlers);

    c.buildings.add(G.nextBuildingId++, BuildingKind::Stockpile, sx, sy, 0);

    for (int32_t k = 0; k < p.settlers; ++k) {
        int32_t x, y;
        int tries = 0;
        do {
            x = sx + rng.range(-4, 4);
            y = sy + rng.range(-4, 4);
        } while ((!g.inBounds(x, y) || g.terrain[g.at(x, y)] == static_cast<int32_t>(Terrain::Water)) &&
                 ++tries < 1000);
        if (tries >= 1000) {
            x = sx;
            y = sy;
        }
        const size_t row = c.settlers.add(G.nextSettlerId++, x, y);
        c.settlers.hunger[row] = rng.below(100);
    }
    c.season = Season::Summer;
    return c;
}

}  // namespace steading
