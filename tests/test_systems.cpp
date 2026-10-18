#include "check.h"
#include "colony/generate.h"
#include "colony/systems.h"
#include "colony/view.h"

using namespace steading;

namespace {
Colony tiny() {
    // A hand-built 12x12 all-grass map: one stockpile at (6,6), no settlers.
    Colony c;
    c.grid.init(12, 12);
    c.globals.stockX = 6;
    c.globals.stockY = 6;
    c.globals.maxSettlers = 100;
    c.globals.rng = 12345;
    c.buildings.add(c.globals.nextBuildingId++, BuildingKind::Stockpile, 6, 6, 0);
    return c;
}
void run(Colony& c, int n, const Stats& s = {}) {
    Derived d = derive(c);
    for (int i = 0; i < n; ++i) tick(c, d, s);
}
}  // namespace

int main() {
    SECTION("a settler walks to a tree, gathers, carries wood home");
    {
        Colony c = tiny();
        c.grid.resType[c.grid.at(9, 6)] = static_cast<int32_t>(Resource::Tree);
        c.grid.resAmount[c.grid.at(9, 6)] = 3;
        c.globals.food = 1000;  // food is plenty, so the settler wants wood
        c.settlers.add(c.globals.nextSettlerId++, 6, 6);
        run(c, 40);
        CHECK_MSG(c.globals.wood == 3, "wood=" + std::to_string(c.globals.wood));
        CHECK(c.grid.resAmount[c.grid.at(9, 6)] == 0);
    }
    SECTION("hunger: eat when food exists, starve when it does not");
    {
        Colony c = tiny();
        c.globals.food = 1;
        c.settlers.add(c.globals.nextSettlerId++, 6, 6);
        c.settlers.hunger[0] = Rules::kMealEvery - 1;
        run(c, 1);
        CHECK(c.globals.food == 0 && c.settlers.hunger[0] == 0);
        c.settlers.hunger[0] = Rules::kStarveAt - 1;
        run(c, 1);
        CHECK_MSG(c.settlers.size() == 0, "a starving settler dies");
    }
    SECTION("huts: built from wood; plans take priority; housing cap grows");
    {
        Colony c = tiny();
        Stats s;
        const int64_t cap0 = housingCap(c, s);
        Derived d = derive(c);
        std::string why;
        CHECK_MSG(planHut(c, d, 1, 1, &why), why);
        CHECK(!planHut(c, d, 1, 1, &why));  // occupied now
        CHECK(!planHut(c, d, 6, 6, &why));  // stockpile
        CHECK(!planHut(c, d, -1, 3, &why)); // out of bounds
        c.globals.wood = 25;
        tick(c, d, s);
        CHECK(c.globals.wood == 5);
        CHECK(c.buildings.kind[1] == static_cast<int32_t>(BuildingKind::Hut) && c.buildings.x[1] == 1);
        CHECK(housingCap(c, s) == cap0 + 4);
        c.globals.wood = 20;
        tick(c, d, s);  // no plan left: automatic placement near the stockpile
        CHECK(c.buildings.size() == 3 && c.buildings.kind[2] == static_cast<int32_t>(BuildingKind::Hut));
        CHECK(c.globals.wood == 0);
        CHECK(planHut(c, d, 10, 10, &why));
        CHECK(cancelPlan(c, d, 10, 10));
        CHECK(!cancelPlan(c, d, 10, 10));
        CHECK(c.buildings.size() == 3);
    }
    SECTION("a mod stat (cheap huts) changes the rules");
    {
        Colony c = tiny();
        Stats cheap;
        cheap.hutWoodCost = 10;
        c.globals.wood = 10;
        Derived d = derive(c);
        tick(c, d, cheap);
        CHECK(c.buildings.size() == 2 && c.globals.wood == 0);
    }
    SECTION("births need food and housing");
    {
        Colony c = tiny();
        c.globals.food = 100;
        run(c, 1);  // tick 0 is a birth tick
        CHECK(c.settlers.size() == 1 && c.globals.food < 100);
        Colony full = tiny();
        full.globals.food = 100;
        for (int i = 0; i < Rules::kBaseCapacity; ++i) full.settlers.add(full.globals.nextSettlerId++, 6, 6);
        run(full, 1);
        CHECK(full.settlers.size() == static_cast<size_t>(Rules::kBaseCapacity));
    }
    SECTION("seasons alternate and gate tree regrowth");
    {
        Colony c = tiny();
        for (int i = 0; i < 144; ++i) {
            c.grid.resType[i] = static_cast<int32_t>(Resource::Tree);
            c.grid.resAmount[i] = 0;
        }
        c.grid.resType[c.grid.at(6, 6)] = 0;
        Stats s;
        s.regrowSummer = 0;
        s.regrowWinter = 1000;
        c.globals.food = 0;
        run(c, Rules::kSeasonLength - 1, s);
        int grown = 0;
        for (int32_t a : c.grid.resAmount) grown += a > 0;
        CHECK_MSG(grown == 0, "no summer regrowth when the summer rate is 0");
        CHECK(c.season == Season::Summer);
        run(c, 1, s);
        CHECK(c.season == Season::Winter);
        run(c, 20, s);
        grown = 0;
        for (int32_t a : c.grid.resAmount) grown += a > 0;
        CHECK_MSG(grown > 0, "winter regrowth when the winter rate is 1000");
        CHECK(wintersSurvived(c) == 0);
        run(c, Rules::kSeasonLength, s);
        CHECK(c.season == Season::Summer && wintersSurvived(c) == 1);
    }
    SECTION("designated tiles are preferred");
    {
        Colony c = tiny();
        c.globals.food = 1000;
        c.grid.resType[c.grid.at(7, 6)] = 1;  // near, not designated
        c.grid.resAmount[c.grid.at(7, 6)] = 5;
        c.grid.resType[c.grid.at(2, 2)] = 1;  // far, designated
        c.grid.resAmount[c.grid.at(2, 2)] = 5;
        CHECK(designate(c, 0, 0, 3, 3, true) == 1);  // only resource tiles are marked
        c.settlers.add(c.globals.nextSettlerId++, 6, 6);
        run(c, 1);
        CHECK(c.settlers.task[0] == static_cast<int32_t>(Task::ToResource));
        CHECK(c.settlers.tx[0] == 2 && c.settlers.ty[0] == 2);
        CHECK(designate(c, 3, 3, 0, 0, false) == 1);
    }
    SECTION("win and lose");
    {
        Colony c = tiny();
        CHECK(outcome(c) == Outcome::Lost);
        c.settlers.add(0, 6, 6);
        CHECK(outcome(c) == Outcome::Playing);
        c.globals.tick = 5 * 2 * Rules::kSeasonLength;
        CHECK(outcome(c) == Outcome::Won);
        Goals g;
        g.winters = 100;
        g.population = 1;
        c.globals.tick = 0;
        CHECK(outcome(c, g) == Outcome::Won);
    }
    SECTION("view projects settlers, buildings and live trees");
    {
        Colony c = tiny();
        c.settlers.add(42, 3, 4);
        c.grid.resType[c.grid.at(1, 1)] = 1;
        c.grid.resAmount[c.grid.at(1, 1)] = 10;
        c.grid.resType[c.grid.at(2, 1)] = 1;  // depleted: not drawn
        auto v = view(c);
        CHECK(v.size() == 3);
        CHECK(v[0].path == "Settlers/S_000042" && v[0].prefab == "/Prefabs/Settler" && v[0].x == 3.5f);
        CHECK(v[1].path == "Buildings/B_000000" && v[1].prefab == "/Prefabs/Stockpile");
        CHECK(v[2].path == "Trees/T_001_001" && v[2].scale == 1.0f);
    }
    return check::finish();
}
