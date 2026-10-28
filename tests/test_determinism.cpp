// Determinism and soak tests. A save round trip can only be exact if the
// saved components are the WHOLE state: a copy of a Colony taken mid-run, with
// its derived caches rebuilt from scratch (as a load does), must keep
// simulating identically to the original.
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "check.h"
#include "colony/generate.h"
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
        // A field added to the schema without a case here fails these counts.
        CHECK(std::size(grid) == std::size(schema::kGridChannels));
        CHECK(std::size(settlers) == std::size(schema::kSettlerFields));
        CHECK(std::size(buildings) == std::size(schema::kBuildingFields));
        CHECK(std::size(globals) == std::size(schema::kGlobals));
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
