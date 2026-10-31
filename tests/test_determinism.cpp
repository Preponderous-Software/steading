// Determinism and soak tests. A save round trip can only be exact if the
// saved components are the WHOLE state: a copy of a Colony taken mid-run, with
// its derived caches rebuilt from scratch (as a load does), must keep
// simulating identically to the original.
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "check.h"
#include "colony/generate.h"
#include "colony/rng.h"
#include "colony/schema.h"
#include "colony/systems.h"

using namespace steading;

namespace {

bool sortedUnique(const std::vector<int64_t>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] <= v[i - 1]) return false;
    return true;
}

// Invariants that must hold after every tick.
std::string invariants(const Colony& c, const Derived& d) {
    const Grid& g = c.grid;
    if (!sortedUnique(c.settlers.id)) return "settler ids not sorted/unique";
    if (!sortedUnique(c.buildings.id)) return "building ids not sorted/unique";
    if (!c.settlers.id.empty() && c.settlers.id.back() >= c.globals.nextSettlerId) return "nextSettlerId reused";
    if (!c.buildings.id.empty() && c.buildings.id.back() >= c.globals.nextBuildingId) return "nextBuildingId reused";
    for (size_t i = 0; i < c.settlers.size(); ++i) {
        if (!g.inBounds(c.settlers.x[i], c.settlers.y[i])) return "settler out of bounds";
        if (g.terrain[g.at(c.settlers.x[i], c.settlers.y[i])] == 1) return "settler in water";
        if (c.settlers.carry[i] < 0 || c.settlers.carry[i] > Rules::kCarryMax) return "carry out of range";
    }
    for (int32_t a : g.resAmount)
        if (a < 0) return "negative resource";
    if (c.globals.wood < 0 || c.globals.food < 0 || c.globals.stone < 0) return "negative stock";
    if (derive(c).occupied != d.occupied) return "derived occupancy drifted from the saved state";
    return "";
}

}  // namespace

int main() {
    SECTION("1,000-tick soak holds every invariant");
    {
        Colony c = generate(GenParams{});
        Derived d = derive(c);
        Stats s;
        std::string bad;
        for (int t = 0; t < 1000 && bad.empty(); ++t) {
            tick(c, d, s);
            bad = invariants(c, d);
            if (!bad.empty()) bad = "tick " + std::to_string(t) + ": " + bad;
        }
        CHECK_MSG(bad.empty(), bad);
        CHECK(c.globals.tick == 1000);
        CHECK_MSG(c.buildings.size() > 1, "the colony builds huts");
        std::printf("  after 1000 ticks: settlers=%zu buildings=%zu wood=%" PRId64 " food=%" PRId64 "\n",
                    c.settlers.size(), c.buildings.size(), c.globals.wood, c.globals.food);
    }
    SECTION("a mid-run copy with rebuilt caches continues identically (save/load precondition)");
    {
        Colony a = generate(GenParams{});
        Derived da = derive(a);
        Stats s;
        for (int t = 0; t < 300; ++t) tick(a, da, s);
        Colony b = a;          // what a perfect save + load yields
        Derived db = derive(b);  // caches are rebuilt, never loaded
        for (int t = 0; t < 700; ++t) {
            tick(a, da, s);
            tick(b, db, s);
        }
        std::string why;
        CHECK_MSG(equal(a, b, &why), why);
    }
    SECTION("equal() and fingerprint() see every saved field");
    {
        // The save round trip and the soak above are only as strong as equal() and
        // fingerprint(): a field either one skips would be dropped by a save without a
        // check noticing. Change each saved field on its own and require both to see it.
        const Colony a = generate(GenParams{});
        const uint64_t fp = fingerprint(a);
        struct Edit {
            const char* field;
            void (*apply)(Colony&);
        };
        const Edit grid[] = {
            {"grid.terrain", [](Colony& col) { col.grid.terrain[0] += 1; }},
            {"grid.resType", [](Colony& col) { col.grid.resType[0] += 1; }},
            {"grid.resAmount", [](Colony& col) { col.grid.resAmount[0] += 1; }},
            {"grid.designated", [](Colony& col) { col.grid.designated[0] += 1; }},
        };
        const Edit settlers[] = {
            {"settlers.x", [](Colony& col) { col.settlers.x[0] += 1; }},
            {"settlers.y", [](Colony& col) { col.settlers.y[0] += 1; }},
            {"settlers.task", [](Colony& col) { col.settlers.task[0] += 1; }},
            {"settlers.tx", [](Colony& col) { col.settlers.tx[0] += 1; }},
            {"settlers.ty", [](Colony& col) { col.settlers.ty[0] += 1; }},
            {"settlers.carry", [](Colony& col) { col.settlers.carry[0] += 1; }},
            {"settlers.carryType", [](Colony& col) { col.settlers.carryType[0] += 1; }},
            {"settlers.hunger", [](Colony& col) { col.settlers.hunger[0] += 1; }},
            {"settlers.patience", [](Colony& col) { col.settlers.patience[0] += 1; }},
        };
        const Edit buildings[] = {
            {"buildings.x", [](Colony& col) { col.buildings.x[0] += 1; }},
            {"buildings.y", [](Colony& col) { col.buildings.y[0] += 1; }},
            {"buildings.builtTick", [](Colony& col) { col.buildings.builtTick[0] += 1; }},
        };
        const Edit globals[] = {
            {"globals.tick", [](Colony& col) { col.globals.tick += 1; }},
            {"globals.rng", [](Colony& col) { col.globals.rng += 1; }},
            {"globals.wood", [](Colony& col) { col.globals.wood += 1; }},
            {"globals.stone", [](Colony& col) { col.globals.stone += 1; }},
            {"globals.food", [](Colony& col) { col.globals.food += 1; }},
            {"globals.stockX", [](Colony& col) { col.globals.stockX += 1; }},
            {"globals.stockY", [](Colony& col) { col.globals.stockY += 1; }},
            {"globals.maxSettlers", [](Colony& col) { col.globals.maxSettlers += 1; }},
            {"globals.nextSettlerId", [](Colony& col) { col.globals.nextSettlerId += 1; }},
            {"globals.nextBuildingId", [](Colony& col) { col.globals.nextBuildingId += 1; }},
            {"globals.seed", [](Colony& col) { col.globals.seed += 1; }},
        };
        // Fields with no per-column schema name: entity ids (the prim names), the
        // building kind (the prefab it references), the grid size and the season.
        const Edit other[] = {
            {"settlers.id", [](Colony& col) { col.settlers.id.back() += 1; }},
            {"buildings.id", [](Colony& col) { col.buildings.id.back() += 1; }},
            {"buildings.kind", [](Colony& col) { col.buildings.kind[0] += 1; }},
            {"grid size", [](Colony& col) { col.grid.w += 1; }},
            {"season", [](Colony& col) { col.season = Season::Winter; }},
        };
        // A field added to the schema without a case here fails to compile.
        static_assert(sizeof grid / sizeof grid[0] == std::size(schema::kGridChannels), "grid channel cases");
        static_assert(sizeof settlers / sizeof settlers[0] == std::size(schema::kSettlerFields), "settler cases");
        static_assert(sizeof buildings / sizeof buildings[0] == std::size(schema::kBuildingFields), "building cases");
        static_assert(sizeof globals / sizeof globals[0] == std::size(schema::kGlobals), "global cases");
        CHECK(a.season == Season::Summer && a.settlers.size() > 0 && a.buildings.size() > 0);
        CHECK(equal(a, a) && fingerprint(Colony(a)) == fp);
        auto expect = [&](const Edit& e) {
            Colony b = a;
            e.apply(b);
            std::string why;
            const bool same = equal(a, b, &why);
            const size_t n = std::strlen(e.field);
            const bool named = why.compare(0, n, e.field) == 0 && (why.size() == n || why[n] == '[');
            CHECK_MSG(!same, std::string("equal() misses ") + e.field);
            CHECK_MSG(named, std::string("equal() names '") + why + "' for " + e.field);
            CHECK_MSG(fingerprint(b) != fp, std::string("fingerprint() misses ") + e.field);
        };
        for (const Edit& e : grid) expect(e);
        for (const Edit& e : settlers) expect(e);
        for (const Edit& e : buildings) expect(e);
        for (const Edit& e : globals) expect(e);
        for (const Edit& e : other) expect(e);
    }
    SECTION("rng: mix64, Rng and valueNoise are pinned and in range");
    {
        // Every world and every tick draws from these, so a change here moves
        // all saves' futures. mix64 is the SplitMix64 finaliser: these are the
        // published reference outputs for SplitMix64 seeded with 0.
        CHECK(mix64(0) == UINT64_C(0xe220a8397b1dcdaf));
        CHECK(mix64(UINT64_C(0x9e3779b97f4a7c15)) == UINT64_C(0x6e789e6aa1b965f4));

        // The whole Rng state is one uint64_t (saved as Globals::rng): an LCG
        // step, with mix64 applied to the output only.
        Rng r{12345};
        CHECK(r.next() == UINT64_C(0xe28de178a35c2c42) && r.state == UINT64_C(0x1c0d57f10c894254));
        CHECK(r.next() == UINT64_C(0x68c46d70c61c1092) && r.state == UINT64_C(0x43f04a6ece53d613));
        CHECK(r.next() == UINT64_C(0x566ad4001df2b13e) && r.state == UINT64_C(0xe2b8410510668fa6));
        Rng copy{r.state};
        CHECK(copy.next() == r.next());  // restoring the state resumes the same sequence

        bool lo = false, hi = false, inRange = true, belowOk = true;
        for (int i = 0; i < 2000; ++i) {
            const int v = r.range(-3, 3);
            inRange = inRange && v >= -3 && v <= 3;
            lo = lo || v == -3;
            hi = hi || v == 3;
            const int b = r.below(7);
            belowOk = belowOk && b >= 0 && b < 7;
            belowOk = belowOk && r.below(1) == 0;
        }
        CHECK_MSG(inRange && lo && hi, "range() is inclusive at both ends");
        CHECK(belowOk);

        // On the integer lattice valueNoise is exactly the corner hash (53 bits
        // scaled into [0, 1)), including negative cells; in between it blends.
        const double scale = 9007199254740992.0;  // 2^53
        CHECK(valueNoise(42, 3.0, 5.0) * scale == 8412913687174903.0);
        CHECK(valueNoise(42, 4.0, 5.0) * scale == 5182047289156008.0);
        CHECK(valueNoise(42, -2.0, -7.0) * scale == 6492508512279940.0);
        CHECK(valueNoise(43, 3.0, 5.0) * scale == 2608104317372823.0);
        const double c0 = valueNoise(42, 3.0, 5.0), c1 = valueNoise(42, 4.0, 5.0);
        CHECK(valueNoise(42, 3.5, 5.0) == c0 + (c1 - c0) * 0.5);  // smoothstep(0.5) == 0.5
        CHECK(std::fabs(valueNoise(42, 4.0 - 1e-9, 5.0) - c1) < 1e-6);  // continuous across a cell edge
        bool unit = true;
        for (int y = -20; y < 20; ++y)
            for (int x = -20; x < 20; ++x) {
                const double v = valueNoise(7, x * 0.37, y * 0.53);
                unit = unit && v >= 0.0 && v < 1.0;
            }
        CHECK_MSG(unit, "valueNoise stays in [0, 1)");
    }
    SECTION("pinned fingerprint: seed 42, 64x64, 30 settlers, 1000 ticks");
    {
        // Pins the rules' exact output on every compiler and platform CI runs.
        // An intentional rules change updates this value in the same PR (and,
        // once saves exist, needs a schema/migration review).
        Colony c = generate(GenParams{});
        Derived d = derive(c);
        for (int t = 0; t < 1000; ++t) tick(c, d, Stats{});
        const uint64_t fp = fingerprint(c);
        std::printf("  fingerprint %016" PRIx64 "\n", fp);
        CHECK_MSG(fp == UINT64_C(0x011ed2b2f364b21b), "rules output changed");
    }
    return check::finish();
}
