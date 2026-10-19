#include "replay.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>

#include "usd_recorder/replay_writer.hpp"

namespace steading::replay {

namespace {

using usd_recorder::Rgb8;

// The game draws linear colours; the replay writer takes sRGB bytes (and stores linear), so
// convert with the exact inverse of the writer's sRGB -> linear transfer.
Rgb8 srgb(const float lin[3]) {
    auto enc = [](float c) {
        const double v = std::clamp(static_cast<double>(c), 0.0, 1.0);
        const double s = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
        return static_cast<uint8_t>(std::lround(s * 255.0));
    };
    return {enc(lin[0]), enc(lin[1]), enc(lin[2])};
}

// Terrain colours, as app/scene.cpp draws the tiles (linear).
const float kGrass[3] = {0.35f, 0.62f, 0.30f}, kGrassWinter[3] = {0.78f, 0.82f, 0.84f},
            kWater[3] = {0.15f, 0.35f, 0.75f}, kRock[3] = {0.60f, 0.58f, 0.55f}, kMarked[3] = {0.95f, 0.55f, 0.15f},
            kStone[3] = {0.42f, 0.42f, 0.46f}, kBerry[3] = {0.62f, 0.25f, 0.55f};

struct Set {
    std::vector<int64_t> ids;
    std::vector<float> xyz;
    std::vector<float> widths;
    std::vector<Rgb8> colors;
    void clear() {
        ids.clear();
        xyz.clear();
        widths.clear();
        colors.clear();
    }
    void add(int64_t id, float x, float y, float z) {
        ids.push_back(id);
        xyz.insert(xyz.end(), {x, y, z});
    }
};

std::string hex16(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016" PRIx64, v);
    return b;
}

}  // namespace

std::vector<Sample> record(Colony& c, Derived& d, const Stats& s, const Colors& colors, const Options& o,
                           const std::string& path) {
    const Grid& g = c.grid;
    const int32_t w = g.w, h = g.h;
    usd_recorder::ReplayMeta meta;
    meta.sim = "steading";
    meta.sim_version = o.simVersion;
    meta.seed = std::to_string(static_cast<uint64_t>(c.globals.seed));
    meta.tick_rate = 10;  // the game's default speed: 10 ticks per second
    meta.playback_rate = o.playbackRate;
    meta.state_hash_algorithm = "steading fingerprint (FNV-1a 64 over every saved field, components.h)";
    meta.title = o.title.empty() ? "steading, seed " + meta.seed : o.title;
    meta.description = "A colony on a " + std::to_string(w) + "x" + std::to_string(h) +
                       " map: settlers gather wood, stone and berries, build huts and live through the winters.";
    meta.source = o.source;
    meta.number_params["width"] = w;
    meta.number_params["height"] = h;
    meta.number_params["ticks"] = static_cast<double>(o.ticks);
    meta.number_params["every"] = static_cast<double>(o.every);
    meta.number_params["firstTick"] = static_cast<double>(c.globals.tick);
    std::string mods;
    for (const auto& m : o.mods) mods += (mods.empty() ? "" : ",") + m;
    meta.string_params["mods"] = mods;

    usd_recorder::ReplayWriter wr(meta);
    // The map lies on the replay's XY plane with row 0 at the top: cell (x, y) is centred at
    // (x + 0.5, h - y - 0.5). Heights follow the game's primitives (app/scene.cpp).
    wr.entity_set("settlers", "Settlers", srgb(colors.settler), 0.55f, "sphere");
    wr.entity_set("huts", "Huts", srgb(colors.hut), 0.85f, "cube");
    wr.entity_set("plans", "Hut plans", srgb(colors.hutPlan), 0.5f, "cube");
    wr.entity_set("stockpile", "Stockpile", srgb(colors.stockpile), 1.0f, "cube");
    wr.entity_set("trees", "Trees", std::nullopt, std::nullopt, "sphere");
    wr.entity_set("stone", "Stone", srgb(kStone), 0.5f, "cube");
    wr.entity_set("berries", "Berries", srgb(kBerry), 0.45f, "sphere");
    wr.field("terrain", "Terrain", w, h, 0.0, 0.0, 1.0, 0.0);

    Set settlers, huts, plans, stock, trees, stone, berries;
    std::vector<Rgb8> terrain(static_cast<size_t>(w) * static_cast<size_t>(h));
    const Rgb8 water = srgb(kWater), rock = srgb(kRock), grass = srgb(kGrass), snow = srgb(kGrassWinter),
               marked = srgb(kMarked), summerTree = srgb(colors.treeSummer), winterTree = srgb(colors.treeWinter);
    auto X = [](int32_t x) { return static_cast<float>(x) + 0.5f; };
    auto Y = [h](int32_t y) { return static_cast<float>(h - y) - 0.5f; };

    std::vector<Sample> out;
    auto sample = [&] {
        const bool winter = c.season == Season::Winter;
        for (Set* st : {&settlers, &huts, &plans, &stock, &trees, &stone, &berries}) st->clear();
        const Settlers& S = c.settlers;
        for (size_t i = 0; i < S.size(); ++i) settlers.add(S.id[i], X(S.x[i]), Y(S.y[i]), 0.35f);
        const Buildings& B = c.buildings;
        for (size_t i = 0; i < B.size(); ++i) {
            const auto k = static_cast<BuildingKind>(B.kind[i]);
            Set& t = k == BuildingKind::Hut ? huts : k == BuildingKind::HutPlan ? plans : stock;
            t.add(B.id[i], X(B.x[i]), Y(B.y[i]), k == BuildingKind::Hut ? 0.45f : 0.1f);
        }
        for (int32_t y = 0; y < h; ++y)
            for (int32_t x = 0; x < w; ++x) {
                const size_t k = g.at(x, y);
                const auto t = static_cast<Terrain>(g.terrain[k]);
                // Field row 0 is the bottom of the map (the replay's minimum y): game row h - 1.
                const size_t cell = static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                terrain[cell] = g.designated[k] ? marked
                                : t == Terrain::Water ? water
                                : t == Terrain::Rock  ? rock
                                : winter              ? snow
                                                      : grass;
                const int amount = g.resAmount[k];
                if (amount <= 0) continue;
                const auto r = static_cast<Resource>(g.resType[k]);
                const auto id = static_cast<int64_t>(k);
                if (r == Resource::Tree) {
                    const float size = 0.3f + 0.06f * static_cast<float>(amount);
                    trees.add(id, X(x), Y(y), size / 2);
                    trees.widths.push_back(size);
                    trees.colors.push_back(winter ? winterTree : summerTree);
                } else if (r == Resource::Stone) {
                    stone.add(id, X(x), Y(y), 0.3f);
                } else if (r == Resource::Berry) {
                    berries.add(id, X(x), Y(y), 0.25f);
                }
            }
        const uint64_t fp = fingerprint(c);
        wr.begin_sample(c.globals.tick, hex16(fp));
        auto put = [&](const char* name, const Set& st, bool perPoint) {
            wr.entities(name, st.ids.data(), st.xyz.data(), st.ids.size(), perPoint ? st.colors.data() : nullptr,
                        perPoint ? st.widths.data() : nullptr);
        };
        put("settlers", settlers, false);
        put("huts", huts, false);
        put("plans", plans, false);
        put("stockpile", stock, false);
        put("trees", trees, true);
        put("stone", stone, false);
        put("berries", berries, false);
        wr.field_colors("terrain", terrain.data());
        wr.stat("settlers", static_cast<double>(S.size()));
        wr.stat("huts", static_cast<double>(huts.ids.size()));
        wr.stat("housing", static_cast<double>(housingCap(c, s)));
        wr.stat("wood", static_cast<double>(c.globals.wood));
        wr.stat("stone", static_cast<double>(c.globals.stone));
        wr.stat("food", static_cast<double>(c.globals.food));
        wr.stat("winters", static_cast<double>(wintersSurvived(c)));
        wr.end_sample();
        out.push_back({c.globals.tick, fp});
    };

    const long every = std::max(1L, o.every);
    sample();
    for (long t = 1; t <= o.ticks; ++t) {
        tick(c, d, s);
        if (t % every == 0 || t == o.ticks) sample();
    }
    wr.write(path);
    return out;
}

}  // namespace steading::replay
