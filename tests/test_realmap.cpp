// A colony on a real map: reading a world package and importing its ground.
//
//  - every ground class maps to the terrain docs/WORLD_PACKAGE.md promises;
//  - an imported colony is well formed, and the same package and seed give the
//    same colony (another seed only moves resources and settlers);
//  - the package reader decodes the checked-in synthetic fixture, and refuses
//    broken packages with a reason;
//  - a map colony plays.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "check.h"
#include "colony/realmap.h"
#include "colony/systems.h"

using namespace steading;

namespace {

const std::string kSource = STEADING_SOURCE_DIR;
const std::vector<std::string> kClasses = {"void", "water", "plain", "forest", "snow", "sand",
                                           "stone", "badland", "ice", "lava", "built", "farm"};
enum : uint8_t { Void, Water, Plain, Forest, Snow, Sand, Stone, Badland, Ice, Lava, Built, Farm };

// A 32x24 map: a forested west, open plain in the middle, a stone ridge with a
// snow cap in the east, a lake, a strip of every other class along the top row
// and a no-data corner.
MapPackage handMade() {
    MapPackage m;
    m.w = 32;
    m.h = 24;
    m.classes = kClasses;
    m.ground.assign(static_cast<size_t>(m.w * m.h), Plain);
    m.hasData.assign(m.ground.size(), 1);
    for (int32_t y = 0; y < m.h; ++y)
        for (int32_t x = 0; x < m.w; ++x) {
            uint8_t& c = m.ground[static_cast<size_t>(y * m.w + x)];
            if (x < 10) c = Forest;
            if (x >= 24) c = x >= 29 ? Snow : Stone;
            if (x >= 13 && x < 17 && y >= 15 && y < 19) c = Water;
        }
    const uint8_t strip[] = {Void, Water, Plain, Forest, Snow, Sand, Stone, Badland, Ice, Lava, Built, Farm};
    for (int32_t x = 0; x < 12; ++x) m.ground[static_cast<size_t>(10 + x)] = strip[x];
    for (int32_t y = 20; y < 24; ++y)
        for (int32_t x = 0; x < 4; ++x) m.hasData[static_cast<size_t>(y * m.w + x)] = 0;
    return m;
}

void writeText(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

void copyFile(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";

    SECTION("ground classes map to steading's terrain");
    {
        bool tree = true;
        CHECK(terrainForClass("water", &tree) == Terrain::Water && !tree);
        CHECK(terrainForClass("ice") == Terrain::Water);
        CHECK(terrainForClass("void") == Terrain::Water);
        CHECK(terrainForClass("stone") == Terrain::Rock);
        CHECK(terrainForClass("snow") == Terrain::Rock);
        CHECK(terrainForClass("lava") == Terrain::Rock);
        CHECK(terrainForClass("forest", &tree) == Terrain::Grass && tree);
        for (const char* cls : {"plain", "sand", "badland", "farm", "built", "something new"}) {
            CHECK_MSG(terrainForClass(cls, &tree) == Terrain::Grass && !tree, cls);
        }
    }

    SECTION("an imported colony is well formed");
    {
        const MapPackage m = handMade();
        GenParams p;
        p.width = p.height = 64;  // ignored: the package sets the size
        p.settlers = 12;
        const Colony c = importMap(m, p);
        const Grid& g = c.grid;
        CHECK(g.w == 32 && g.h == 24 && g.cells() == m.ground.size());
        int trees = 0, stone = 0, forestTrees = 0, forestTiles = 0;
        for (int32_t y = 0; y < g.h; ++y)
            for (int32_t x = 0; x < g.w; ++x) {
                const size_t i = g.at(x, y);
                const Terrain want = m.hasData[i] ? terrainForClass(kClasses[m.ground[i]]) : Terrain::Water;
                CHECK_MSG(g.terrain[i] == static_cast<int32_t>(want), std::to_string(x) + "," + std::to_string(y));
                CHECK(g.designated[i] == 0);
                if (g.resType[i] == static_cast<int32_t>(Resource::Tree)) {
                    ++trees;
                    CHECK(m.ground[i] == Forest && m.hasData[i]);  // trees only on forest
                }
                if (g.resType[i] == static_cast<int32_t>(Resource::Stone)) {
                    ++stone;
                    CHECK(g.terrain[i] == static_cast<int32_t>(Terrain::Rock));
                    CHECK(x < 27 || y == 0);  // the ridge's west flank (and the strip), not its heart
                }
                if (g.resType[i] == static_cast<int32_t>(Resource::Berry)) CHECK(g.terrain[i] == 0 && m.ground[i] != Forest);
                if (m.ground[i] == Forest && m.hasData[i] && y > 0) {
                    ++forestTiles;
                    forestTrees += g.resType[i] == static_cast<int32_t>(Resource::Tree);
                }
            }
        CHECK_MSG(trees > 0 && stone > 0, "trees and stone to gather");
        CHECK_MSG(forestTrees * 3 > forestTiles && forestTrees < forestTiles, "a walkable forest, about half trees");
        CHECK(c.buildings.size() == 1 && c.buildings.kind[0] == static_cast<int32_t>(BuildingKind::Stockpile));
        const size_t sp = g.at(c.buildings.x[0], c.buildings.y[0]);
        CHECK(g.terrain[sp] == 0 && g.resType[sp] == 0);
        CHECK(c.settlers.size() == 12 && c.globals.maxSettlers == 24 && c.globals.seed == 42);
        for (size_t i = 0; i < c.settlers.size(); ++i)
            CHECK(g.inBounds(c.settlers.x[i], c.settlers.y[i]) &&
                  g.terrain[g.at(c.settlers.x[i], c.settlers.y[i])] != static_cast<int32_t>(Terrain::Water));
    }

    SECTION("same package and seed -> same colony; another seed keeps the ground");
    {
        const MapPackage m = handMade();
        GenParams p;
        const Colony a = importMap(m, p), b = importMap(m, p);
        std::string why;
        CHECK_MSG(equal(a, b, &why), why);
        CHECK(fingerprint(a) == fingerprint(b));
        p.seed = 7;
        const Colony other = importMap(m, p);
        CHECK(!equal(a, other));
        CHECK(a.grid.terrain == other.grid.terrain);
    }

    SECTION("maps that cannot hold a colony are refused");
    {
        auto refused = [](const MapPackage& m) {
            try {
                importMap(m, GenParams{});
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
        MapPackage m = handMade();
        m.w = 4;
        CHECK(refused(m));
        MapPackage wet = handMade();
        wet.ground.assign(wet.ground.size(), Water);
        CHECK(refused(wet));
        MapPackage big = handMade();
        big.w = kMaxMapSide + 1;
        big.ground.assign(static_cast<size_t>(big.w * big.h), Plain);
        big.hasData.assign(big.ground.size(), 1);
        CHECK(refused(big));
    }

    const std::string fixture = kSource + "/tests/maps/island";
    SECTION("the reader decodes the synthetic fixture package");
    {
        MapPackage m;
        std::string err;
        CHECK_MSG(readMapPackage(fixture, m, &err), err);
        CHECK(m.w == 48 && m.h == 48 && m.classes == kClasses);
        CHECK(m.ground.size() == 48u * 48u && m.hasData.size() == m.ground.size());
        int counts[12] = {}, data = 0;
        for (size_t i = 0; i < m.ground.size(); ++i) {
            ++counts[m.ground[i]];
            data += m.hasData[i];
        }
        // Counted from the PNGs when the fixture was made (see tests/maps/README.md).
        CHECK(data == 48 * 48);
        CHECK_MSG(counts[Water] == 50 && counts[Plain] == 1315 && counts[Forest] == 269 &&
                      counts[Sand] == 143 && counts[Stone] == 371 && counts[Snow] == 156,
                  "class counts");
        CHECK(m.ground[0] == Forest && m.ground[48 * 48 - 1] == Plain);
    }

    SECTION("a map colony plays");
    {
        MapPackage m;
        std::string err;
        CHECK_MSG(readMapPackage(fixture, m, &err), err);
        Colony c = importMap(m, GenParams{});
        Derived d = derive(c);
        const Stats s;
        for (int t = 0; t < 600; ++t) tick(c, d, s);
        CHECK_MSG(c.settlers.size() > 0 && c.globals.wood + c.globals.stone > 0, "settlers gathered");
        CHECK(c.buildings.size() > 1);  // huts went up
    }

    SECTION("broken packages are refused with a reason");
    {
        auto refusal = [&](const std::string& name, const std::string& worldJson, const std::string& expect,
                           bool copyRasters = true) {
            const std::string dir = out + "/realmap-" + name;
            std::error_code ec;
            std::filesystem::remove_all(std::filesystem::u8path(dir), ec);
            std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
            if (!worldJson.empty()) writeText(dir + "/world.json", worldJson);
            if (copyRasters) {
                copyFile(fixture + "/height.png", dir + "/height.png");
                copyFile(fixture + "/ground.png", dir + "/ground.png");
            }
            MapPackage m;
            std::string err;
            const bool ok = readMapPackage(dir, m, &err);
            CHECK_MSG(!ok && err.find(expect) != std::string::npos, name + ": " + err);
        };
        const std::string classes = R"("classes": ["void","water","plain","forest"])";
        refusal("missing", "", "no world.json", false);
        refusal("syntax", "{\"grid\": {", "world.json:");
        refusal("nogrid", "{" + classes + "}", "grid.width");
        refusal("huge", R"({"grid": {"width": 4000, "height": 48}, )" + classes + "}", "crop it first");
        refusal("size", R"({"grid": {"width": 40, "height": 48}, "classes": [")" + std::string("void\"]}"),
                "do not match");
        refusal("escape", R"({"grid": {"width": 48, "height": 48}, )" + classes +
                              R"(, "files": {"height": "../height.png"}})", "inside the package");
        refusal("classes", R"({"grid": {"width": 48, "height": 48}, )" + classes + "}", "names only 4");
        refusal("norasters", R"({"grid": {"width": 48, "height": 48}, )" + classes + "}", "cannot read", false);
    }
    return check::finish();
}
