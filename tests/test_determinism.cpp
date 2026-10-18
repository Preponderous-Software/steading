// Determinism and soak tests. A save round trip can only be exact if the
// saved components are the WHOLE state: a copy of a Colony taken mid-run, with
// its derived caches rebuilt from scratch (as a load does), must keep
// simulating identically to the original.
#include <cinttypes>
#include <cstdio>

#include "check.h"
#include "colony/generate.h"
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
