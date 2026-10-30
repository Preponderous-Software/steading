#include <stdexcept>

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
    SECTION("components: remove is a stable erase that keeps every column aligned; ids must increase");
    {
        Colony c = tiny();
        for (int32_t i = 0; i < 3; ++i) {
            const size_t row = c.settlers.add(10 + i, i, 2 * i);
            CHECK(row == static_cast<size_t>(i));
            c.settlers.task[row] = 1 + i;
            c.settlers.tx[row] = 20 + i;
            c.settlers.ty[row] = 30 + i;
            c.settlers.carry[row] = 40 + i;
            c.settlers.carryType[row] = 50 + i;
            c.settlers.hunger[row] = 60 + i;
            c.settlers.patience[row] = 70 + i;
        }
        c.settlers.remove(1);
        const Settlers& s = c.settlers;
        CHECK(s.size() == 2 && s.id[0] == 10 && s.id[1] == 12);
        for (const auto* col : {&s.x, &s.y, &s.task, &s.tx, &s.ty, &s.carry, &s.carryType, &s.hunger, &s.patience})
            CHECK(col->size() == 2);
        CHECK(s.x[1] == 2 && s.y[1] == 4 && s.task[1] == 3 && s.tx[1] == 22 && s.ty[1] == 32);
        CHECK(s.carry[1] == 42 && s.carryType[1] == 52 && s.hunger[1] == 62 && s.patience[1] == 72);
        CHECK(s.x[0] == 0 && s.patience[0] == 70);

        bool threw = false;
        try { c.settlers.add(12, 0, 0); } catch (const std::logic_error&) { threw = true; }
        CHECK_MSG(threw, "a settler id not above every existing id is refused");
        CHECK(c.settlers.size() == 2);

        c.buildings.add(5, BuildingKind::Hut, 1, 2, 7);
        c.buildings.add(6, BuildingKind::HutPlan, 3, 4, 8);
        c.buildings.remove(0);  // the stockpile from tiny()
        const Buildings& b = c.buildings;
        CHECK(b.size() == 2 && b.id[0] == 5 && b.id[1] == 6);
        CHECK(b.kind.size() == 2 && b.x.size() == 2 && b.y.size() == 2 && b.builtTick.size() == 2);
        CHECK(b.kind[1] == static_cast<int32_t>(BuildingKind::HutPlan) && b.x[1] == 3 && b.y[1] == 4 &&
              b.builtTick[1] == 8);
        threw = false;
        try { c.buildings.add(6, BuildingKind::Hut, 0, 0, 0); } catch (const std::logic_error&) { threw = true; }
        CHECK_MSG(threw, "a building id not above every existing id is refused");
        CHECK(c.buildings.size() == 2);
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
    SECTION("prefab paths and prim names are pinned (a building's kind is saved as its prefab path)");
    {
        CHECK(std::string(prefabFor(BuildingKind::Stockpile)) == "/Prefabs/Stockpile");
        CHECK(std::string(prefabFor(BuildingKind::Hut)) == "/Prefabs/Hut");
        CHECK(std::string(prefabFor(BuildingKind::HutPlan)) == "/Prefabs/HutPlan");
        CHECK_MSG(std::string(prefabFor(static_cast<BuildingKind>(99))) == "/Prefabs/Unknown",
                  "an out-of-range kind maps to no real prefab");
        CHECK(primName("S_", 0) == "S_000000");
        CHECK(primName("B_", 42) == "B_000042");
        CHECK_MSG(primName("S_", 1234567) == "S_1234567", "ids past six digits widen, never truncate");

        Colony c = tiny();
        c.buildings.add(c.globals.nextBuildingId++, BuildingKind::Hut, 2, 3, 0);
        c.buildings.add(c.globals.nextBuildingId++, BuildingKind::HutPlan, 4, 5, 0);
        c.grid.resType[c.grid.at(7, 8)] = static_cast<int32_t>(Resource::Tree);
        c.grid.resAmount[c.grid.at(7, 8)] = 4;
        c.grid.resType[c.grid.at(8, 8)] = static_cast<int32_t>(Resource::Stone);  // not a tree: not drawn
        c.grid.resAmount[c.grid.at(8, 8)] = 5;
        auto v = view(c);
        CHECK(v.size() == 4);
        CHECK(v[1].path == "Buildings/B_000001" && v[1].prefab == "/Prefabs/Hut" && v[1].x == 2.5f &&
              v[1].y == 3.5f);
        CHECK(v[2].path == "Buildings/B_000002" && v[2].prefab == "/Prefabs/HutPlan");
        CHECK(v[3].path == "Trees/T_007_008" && v[3].prefab == "/Prefabs/Tree" && v[3].scale == 0.5f + 0.05f * 4.f);
    }
    SECTION("tasks: a returning settler delivers stone and berries to their own stocks");
    {
        Colony c = tiny();
        Rng rng{1};
        const size_t i = c.settlers.add(c.globals.nextSettlerId++, 6, 6);  // standing on the stockpile
        c.settlers.task[i] = static_cast<int32_t>(Task::Return);
        c.settlers.carry[i] = 3;
        c.settlers.carryType[i] = static_cast<int32_t>(Resource::Stone);
        c.settlers.patience[i] = 7;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.globals.stone == 3 && c.globals.wood == 0 && c.globals.food == 0);
        CHECK(c.settlers.carry[i] == 0 && c.settlers.carryType[i] == 0);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Idle) && c.settlers.patience[i] == 0);
        c.settlers.task[i] = static_cast<int32_t>(Task::Return);
        c.settlers.carry[i] = 4;
        c.settlers.carryType[i] = static_cast<int32_t>(Resource::Berry);
        systems::tasks(c, Stats{}, rng);
        CHECK(c.globals.food == 4 && c.globals.stone == 3 && c.settlers.carry[i] == 0);
    }
    SECTION("tasks: gathering stops at the carry limit, or goes idle on an empty tile with nothing carried");
    {
        Colony c = tiny();
        Rng rng{1};
        const size_t k = c.grid.at(7, 6);
        c.grid.resType[k] = static_cast<int32_t>(Resource::Tree);
        c.grid.resAmount[k] = Rules::kCarryMax + 2;
        const size_t i = c.settlers.add(c.globals.nextSettlerId++, 7, 6);
        c.settlers.task[i] = static_cast<int32_t>(Task::Gather);
        c.settlers.tx[i] = 7;
        c.settlers.ty[i] = 6;
        for (int n = 0; n < Rules::kCarryMax; ++n) systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.carry[i] == Rules::kCarryMax && c.grid.resAmount[k] == 2);
        CHECK(c.settlers.carryType[i] == static_cast<int32_t>(Resource::Tree));
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Gather));
        systems::tasks(c, Stats{}, rng);
        CHECK_MSG(c.settlers.task[i] == static_cast<int32_t>(Task::Return), "a full settler heads home");
        CHECK(c.grid.resAmount[k] == 2);

        c.grid.resAmount[k] = 0;
        c.settlers.carry[i] = 0;
        c.settlers.task[i] = static_cast<int32_t>(Task::Gather);
        systems::tasks(c, Stats{}, rng);
        CHECK_MSG(c.settlers.task[i] == static_cast<int32_t>(Task::Idle), "nothing to gather, nothing carried");
    }
    SECTION("tasks: patience runs out en route, then a wandering cooldown");
    {
        Colony c = tiny();
        Rng rng{1};
        const size_t i = c.settlers.add(c.globals.nextSettlerId++, 0, 0);
        c.settlers.task[i] = static_cast<int32_t>(Task::ToResource);
        c.settlers.tx[i] = 11;
        c.settlers.ty[i] = 11;
        c.settlers.patience[i] = Rules::kGiveUpToResource - 1;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::ToResource));
        CHECK(c.settlers.patience[i] == Rules::kGiveUpToResource);
        systems::tasks(c, Stats{}, rng);
        CHECK_MSG(c.settlers.task[i] == static_cast<int32_t>(Task::Idle), "gives up after the limit");
        CHECK(c.settlers.patience[i] == -Rules::kWanderCooldown);

        // While cooling down an idle settler ignores resources; once it ends it picks one.
        c.grid.resType[c.grid.at(9, 9)] = static_cast<int32_t>(Resource::Tree);
        c.grid.resAmount[c.grid.at(9, 9)] = 5;
        c.settlers.x[i] = 6;
        c.settlers.y[i] = 6;
        c.settlers.patience[i] = -1;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Idle) && c.settlers.patience[i] == 0);
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::ToResource));
        CHECK(c.settlers.tx[i] == 9 && c.settlers.ty[i] == 9);
    }
    SECTION("tasks: arriving starts gathering; a returning settler that gives up keeps its load");
    {
        Colony c = tiny();
        Rng rng{1};
        const size_t i = c.settlers.add(c.globals.nextSettlerId++, 3, 3);
        c.settlers.task[i] = static_cast<int32_t>(Task::ToResource);
        c.settlers.tx[i] = 3;
        c.settlers.ty[i] = 3;
        c.settlers.patience[i] = 9;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Gather) && c.settlers.patience[i] == 0);

        c.settlers.task[i] = static_cast<int32_t>(Task::Return);
        c.settlers.carry[i] = 2;
        c.settlers.carryType[i] = static_cast<int32_t>(Resource::Tree);
        c.settlers.patience[i] = Rules::kGiveUpReturning;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Return));
        CHECK(c.settlers.patience[i] == -Rules::kWanderCooldown && c.settlers.carry[i] == 2);
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.patience[i] == -Rules::kWanderCooldown + 1 && c.settlers.carry[i] == 2);
        CHECK(c.globals.wood == 0);
    }
    SECTION("tasks: a starving settler is skipped");
    {
        Colony c = tiny();
        Rng rng{1};
        c.grid.resType[c.grid.at(7, 6)] = static_cast<int32_t>(Resource::Tree);
        c.grid.resAmount[c.grid.at(7, 6)] = 5;
        const size_t i = c.settlers.add(c.globals.nextSettlerId++, 5, 5);
        c.settlers.hunger[i] = Rules::kStarveAt;
        const uint64_t before = rng.state;
        systems::tasks(c, Stats{}, rng);
        CHECK(c.settlers.task[i] == static_cast<int32_t>(Task::Idle));
        CHECK(c.settlers.x[i] == 5 && c.settlers.y[i] == 5);
        CHECK_MSG(rng.state == before, "no random draw is spent on a dying settler");
    }
    SECTION("growth: births only on birth ticks, with enough food, under the colony's maxSettlers");
    {
        Colony c = tiny();
        c.globals.food = 100;
        c.globals.tick = Rules::kBirthEvery + 1;
        systems::growth(c, Stats{});
        CHECK(c.settlers.size() == 0 && c.globals.food == 100);
        c.globals.tick = Rules::kBirthEvery;
        c.globals.food = Rules::kBirthFood - 1;
        systems::growth(c, Stats{});
        CHECK(c.settlers.size() == 0);
        c.globals.food = Rules::kBirthFood;
        systems::growth(c, Stats{});
        CHECK(c.settlers.size() == 1 && c.globals.food == 0);
        CHECK(c.settlers.x[0] == 6 && c.settlers.y[0] == 6 && c.globals.nextSettlerId == 1);
        c.globals.food = 100;
        c.globals.maxSettlers = 1;  // below the housing cap of tiny()
        systems::growth(c, Stats{});
        CHECK_MSG(c.settlers.size() == 1 && c.globals.food == 100, "maxSettlers caps births");
    }
    SECTION("designate clamps a rectangle that runs off the map");
    {
        Colony c = tiny();
        c.grid.resType[c.grid.at(0, 0)] = static_cast<int32_t>(Resource::Stone);
        c.grid.resType[c.grid.at(11, 11)] = static_cast<int32_t>(Resource::Berry);
        CHECK(designate(c, -5, -5, 100, 100, true) == 2);
        CHECK(c.grid.designated[c.grid.at(0, 0)] == 1 && c.grid.designated[c.grid.at(11, 11)] == 1);
        CHECK(c.grid.designated[c.grid.at(5, 5)] == 0);
        CHECK(designate(c, 100, 100, -5, -5, true) == 0);  // already marked
        CHECK(designate(c, 20, 20, 30, 30, false) == 0);   // wholly off the map
        CHECK(designate(c, 11, 11, 50, 50, false) == 1);
    }
    return check::finish();
}
