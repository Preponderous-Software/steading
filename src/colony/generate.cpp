#include "generate.h"

#include <stdexcept>

#include "rng.h"
#include "stats.h"
#include "strata/sim/noise.hpp"

namespace steading {

namespace {

// Terrain and forest come from Strata's fractal noise (third_party/strata_noise,
// strata/sim/noise.hpp), so rock outcrops, forest edges and lake shores are
// ragged, as natural edges are, instead of the round blobs that one or two
// octaves of value noise give. noise.hpp's measured table: with octaves down
// to the finest scale that can show, gain sets an edge's fractal dimension;
// gain 0.65 gives about 1.2 (natural coastlines are about 1.1-1.3).
//
// A cell is the finest scale here. Elevation's first layer is 16 cells across:
// rock is cut from five layers (down to one cell), forest from three layers of
// its own (7 cells across down to under two). Water is cut from the first
// three layers of the same elevation (down to four cells): settlers have no
// pathfinding (systems.cpp steps greedily and slides along water), and one-
// or two-cell inlets in a shore trap them, which in a 1,000-seed run with no
// player cost about one colony in twenty. Three layers keep shores irregular
// without the small inlets, and the colonies' survival as it was.
//
// The thresholds make the average share of water, rock and forest (the forest
// mask on grass) over seeds 1-256 the old generator's: about 11%, 9.5% and 34%.
constexpr float kFractalGain = 0.65f;
constexpr int kRockOctaves = 5, kWaterOctaves = 3;

strata::sim::FbmParams elevationNoise(uint64_t seed, int octaves) {
    strata::sim::FbmParams p;
    p.octaves = octaves;
    p.gain = kFractalGain;
    p.frequency = 1.0f / 16.0f;
    p.seed = static_cast<uint32_t>(mix64(seed ^ 0xE1E7ULL));
    return p;
}

strata::sim::FbmParams forestNoise(uint64_t seed) {
    strata::sim::FbmParams p;
    p.octaves = 3;
    p.gain = kFractalGain;
    p.frequency = 1.0f / 7.0f;
    p.seed = static_cast<uint32_t>(mix64(seed ^ 0xF0E57ULL));
    return p;
}

constexpr double kWaterBelow = -0.33, kRockAbove = 0.30, kForestAbove = 0.05;

}  // namespace

// The stockpile, the settlers and the colony-wide state, on a filled grid. `rng`
// continues the stream that filled the grid.
void settle(Colony& c, const GenParams& p, Rng& rng) {
    Grid& g = c.grid;
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
}

Colony generate(const GenParams& p) {
    if (p.width < 8 || p.height < 8) throw std::invalid_argument("map must be at least 8x8");
    if (p.settlers < 0) throw std::invalid_argument("settlers must be >= 0");
    Colony c;
    Grid& g = c.grid;
    g.init(p.width, p.height);
    Rng rng{mix64(p.seed)};
    const strata::sim::FbmParams shoreP = elevationNoise(p.seed, kWaterOctaves),
                                 rockP = elevationNoise(p.seed, kRockOctaves), forestP = forestNoise(p.seed);
    for (int32_t y = 0; y < g.h; ++y)
        for (int32_t x = 0; x < g.w; ++x) {
            const size_t i = g.at(x, y);
            const float fx = static_cast<float>(x), fy = static_cast<float>(y);
            const double shore = strata::sim::fbm(fx, fy, shoreP);
            const double elevation = strata::sim::fbm(fx, fy, rockP);
            const double forest = strata::sim::fbm(fx, fy, forestP);
            const int roll = rng.below(1000);
            if (shore < kWaterBelow) {
                g.terrain[i] = static_cast<int32_t>(Terrain::Water);
            } else if (elevation > kRockAbove) {
                g.terrain[i] = static_cast<int32_t>(Terrain::Rock);
                if (roll < 300) {
                    g.resType[i] = static_cast<int32_t>(Resource::Stone);
                    g.resAmount[i] = Rules::kStoneAmount;
                }
            } else {
                g.terrain[i] = static_cast<int32_t>(Terrain::Grass);
                if (forest > kForestAbove && roll < 450) {
                    g.resType[i] = static_cast<int32_t>(Resource::Tree);
                    g.resAmount[i] = Rules::kTreeAmount;
                } else if (roll < 25) {
                    g.resType[i] = static_cast<int32_t>(Resource::Berry);
                    g.resAmount[i] = Rules::kBerryAmount;
                }
            }
        }

    settle(c, p, rng);
    return c;
}

}  // namespace steading
