#include <cstdio>
#include <set>
#include <stdexcept>

#include "check.h"
#include "colony/generate.h"
#include "colony/systems.h"

using namespace steading;

int main() {
    SECTION("same seed gives an identical colony; another seed differs");
    {
        GenParams p;
        const Colony a = generate(p), b = generate(p);
        std::string why;
        CHECK_MSG(equal(a, b, &why), why);
        CHECK(fingerprint(a) == fingerprint(b));
        p.seed = 7;
        CHECK(!equal(a, generate(p)));
    }
    SECTION("the world is well formed");
    {
        GenParams p;
        p.width = 80;
        p.height = 48;
        p.settlers = 25;
        const Colony c = generate(p);
        const Grid& g = c.grid;
        CHECK(g.w == 80 && g.h == 48 && g.terrain.size() == 80u * 48u);
        int grass = 0, water = 0, rock = 0, trees = 0, berries = 0, stone = 0;
        for (size_t i = 0; i < g.cells(); ++i) {
            grass += g.terrain[i] == 0;
            water += g.terrain[i] == 1;
            rock += g.terrain[i] == 2;
            trees += g.resType[i] == 1;
            stone += g.resType[i] == 2;
            berries += g.resType[i] == 3;
            CHECK(g.terrain[i] >= 0 && g.terrain[i] <= 2);
            CHECK(g.designated[i] == 0);
            if (g.resType[i] == 2) CHECK(g.terrain[i] == 2);  // stone only on rock
            if (g.resType[i] == 1 || g.resType[i] == 3) CHECK(g.terrain[i] == 0);
        }
        CHECK_MSG(grass > water && rock > 0 && trees > 0 && berries > 0 && stone > 0, "a playable mix of terrain and resources");
        // Stockpile is building 0, on clear grass; ids are dense from 0.
        CHECK(c.buildings.size() == 1 && c.buildings.id[0] == 0);
        CHECK(c.buildings.kind[0] == static_cast<int32_t>(BuildingKind::Stockpile));
        const size_t sp = g.at(c.buildings.x[0], c.buildings.y[0]);
        CHECK(g.terrain[sp] == 0 && g.resType[sp] == 0);
        CHECK(c.globals.stockX == c.buildings.x[0] && c.globals.stockY == c.buildings.y[0]);
        CHECK(c.settlers.size() == 25 && c.globals.nextSettlerId == 25 && c.globals.nextBuildingId == 1);
        std::set<int64_t> ids(c.settlers.id.begin(), c.settlers.id.end());
        CHECK(ids.size() == 25);
        for (size_t i = 0; i < c.settlers.size(); ++i) {
            CHECK(g.inBounds(c.settlers.x[i], c.settlers.y[i]));
            CHECK(g.terrain[g.at(c.settlers.x[i], c.settlers.y[i])] != 1);  // never in water
            CHECK(c.settlers.hunger[i] >= 0 && c.settlers.hunger[i] < 100);
        }
        CHECK(c.globals.maxSettlers == 50 && c.globals.food == 20 && c.season == Season::Summer);
        CHECK(c.globals.seed == 42);
    }
    SECTION("the land's shares stay where the two-octave generator had them");
    {
        // World generation moved to fractal noise (generate.cpp). The thresholds were
        // chosen so that, over many seeds, the shares of water, rock and resources stay
        // what the earlier generator gave (seeds 1-256, 64 x 64: water 11.1%, rock 9.5%,
        // trees 15.3%, stone 2.8%, berries 1.1%), so a colony's food, wood and stone do not
        // move. Averaged over 64 seeds here, with a margin.
        double water = 0, rock = 0, trees = 0, stone = 0, berries = 0;
        const int seeds = 64;
        for (int s = 1; s <= seeds; ++s) {
            GenParams p;
            p.seed = static_cast<uint64_t>(s);
            p.settlers = 0;
            const Grid& g = generate(p).grid;
            const double n = static_cast<double>(g.cells()) * seeds;
            for (size_t i = 0; i < g.cells(); ++i) {
                water += (g.terrain[i] == 1) / n;
                rock += (g.terrain[i] == 2) / n;
                trees += (g.resType[i] == 1) / n;
                stone += (g.resType[i] == 2) / n;
                berries += (g.resType[i] == 3) / n;
            }
        }
        std::printf("  water %.3f rock %.3f trees %.3f stone %.3f berries %.4f\n", water, rock, trees, stone, berries);
        CHECK(water > 0.09 && water < 0.135);
        CHECK(rock > 0.075 && rock < 0.115);
        CHECK(trees > 0.135 && trees < 0.17);
        CHECK(stone > 0.02 && stone < 0.035);
        CHECK(berries > 0.009 && berries < 0.0135);
    }
    SECTION("bad parameters are rejected");
    {
        GenParams p;
        p.width = 4;
        bool threw = false;
        try { generate(p); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }
    return check::finish();
}
