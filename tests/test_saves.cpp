// steading's saves through Strata's save core: the M2 "done" checks.
//
//  - a 1,000-tick soak that saves every 100 ticks (alternating sync and async),
//    reloads after every save and requires the reloaded colony to equal the
//    live one exactly, then keeps playing from the reloaded copy;
//  - after a restart the last save loads and both copies stay identical for
//    200 more ticks;
//  - player orders (designations, hut plans) survive a save;
//  - mods change stats and looks, a mod mismatch is refused, a forced load
//    keeps the colony and reverts the stats;
//  - a damaged latest save falls back to the previous one, loudly.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "bridge/bridge.h"
#include "bridge/session.h"
#include "check.h"
#include "colony/schema.h"

using namespace steading;

namespace {

std::string root;
const std::string kGame = STEADING_SOURCE_DIR;

std::string freshDir(const std::string& name) {
    const std::string d = root + "/" + name;
    if (std::system(("rm -rf '" + d + "' && mkdir -p '" + d + "'").c_str()) != 0) std::printf("  rm failed\n");
    return d;
}

std::unique_ptr<Session> open(const std::string& dir, std::vector<std::string> mods = {}) {
    std::string err;
    auto s = Session::open({kGame, dir, std::move(mods)}, &err);
    if (!s) std::printf("  open failed: %s\n", err.c_str());
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    root = argc > 1 ? argv[1] : "/tmp/steading-save-tests";
    if (std::system(("mkdir -p '" + root + "'").c_str()) != 0) return 1;

    SECTION("components <-> Strata world is lossless");
    {
        Colony c = generate(GenParams{});
        Derived d = derive(c);
        for (int t = 0; t < 250; ++t) tick(c, d, Stats{});
        designate(c, 0, 0, 20, 20, true);
        std::string why;
        CHECK(planHut(c, d, c.grid.w - 3, c.grid.h - 3, &why) || !why.empty());
        Colony back;
        CHECK_MSG(bridge::fromWorld(bridge::toWorld(c), back, &why), why);
        CHECK_MSG(equal(back, c, &why), why);
        auto bad = bridge::toWorld(c);
        bad.tables[0].ints[0][0] = 9999;  // a settler off the map
        CHECK(!bridge::fromWorld(bad, back, &why));
    }

    SECTION("1,000-tick soak: save every 100 ticks, reload, exact, keep playing from the reload");
    const std::string dir = freshDir("soak");
    Colony finalState;
    {
        auto s = open(dir);
        std::string err;
        CHECK_MSG(s && s->newGame(GenParams{}, &err), err);
        int exact = 0;
        for (int round = 1; round <= 10; ++round) {
            for (int t = 0; t < 100; ++t) s->tick();
            if (round == 3) designate(s->colony(), 5, 5, 30, 30, true);
            if (round == 4) planHut(s->colony(), s->derived(), 2, s->colony().grid.h - 2);
            const Colony live = s->colony();
            strata::save::SaveResult r = round % 2 ? s->save() : s->saveAsync().get();
            CHECK_MSG(r.ok, r.error);
            auto l = s->load();
            std::string why;
            CHECK_MSG(l.ok && !l.usedFallback, l.error);
            if (equal(s->colony(), live, &why)) ++exact;
            else std::printf("  round %d: %s\n", round, why.c_str());
        }
        CHECK_MSG(exact == 10, std::to_string(exact) + "/10 reloads exact");
        CHECK(s->colony().globals.tick == 1000);
        finalState = s->colony();
        std::printf("  tick 1000: settlers=%zu buildings=%zu wood=%lld\n", finalState.settlers.size(),
                    finalState.buildings.size(), static_cast<long long>(finalState.globals.wood));
    }
    SECTION("restart: the last save loads and plays on identically");
    {
        auto s = open(dir);
        auto l = s->load();
        std::string why;
        CHECK_MSG(l.ok && equal(s->colony(), finalState, &why), l.error + why);
        Colony a = finalState;
        Derived da = derive(a);
        for (int t = 0; t < 200; ++t) {
            tick(a, da, s->stats());
            s->tick();
        }
        CHECK_MSG(equal(a, s->colony(), &why), why);
    }

    SECTION("mods: stats and looks compose; mismatch refused; forced load reverts stats");
    {
        const std::string md = freshDir("mods");
        auto vanilla = open(md);
        CHECK(vanilla->stats().hutWoodCost == 20 && vanilla->stats().hutCapacity == 4);
        CHECK(vanilla->stats().regrowSummer == 40 && vanilla->stats().regrowWinter == 0);
        vanilla.reset();
        auto frost = open(freshDir("frost"), {"frost_huts"});
        CHECK(frost->stats().hutCapacity == 8 && frost->looks().hut[2] > 0.9f);
        frost.reset();
        auto both = open(freshDir("both"), {"frost_huts", "cheap_huts"});
        CHECK(both->stats().hutWoodCost == 10 && both->stats().hutCapacity == 6);
        both.reset();
        auto rev = open(freshDir("rev"), {"cheap_huts", "frost_huts"});
        CHECK(rev->stats().hutWoodCost == 10 && rev->stats().hutCapacity == 8);
        rev.reset();
        auto cheap = open(md, {"cheap_huts"});
        std::string err;
        CHECK_MSG(cheap->newGame(GenParams{}, &err), err);
        for (int t = 0; t < 300; ++t) cheap->tick();
        CHECK(cheap->save().ok);
        const Colony saved = cheap->colony();
        cheap.reset();
        auto none = open(md);
        auto l = none->load();
        CHECK_MSG(!l.ok && l.error.find("mod list differs") != std::string::npos, l.error);
        strata::save::LoadOptions force;
        force.allowModMismatch = true;
        l = none->load(force);
        std::string why;
        CHECK_MSG(l.ok && equal(none->colony(), saved, &why), l.error + why);
        CHECK(none->stats().hutWoodCost == 20);
        auto unknown = Session::open({kGame, freshDir("unknown"), {"no_such_mod"}}, &err);
        CHECK_MSG(!unknown && err.find("not installed") != std::string::npos, err);
    }

    SECTION("a damaged latest save falls back to the previous one, loudly");
    {
        const std::string fd = freshDir("fallback");
        auto s = open(fd);
        std::string err;
        CHECK(s->newGame(GenParams{}, &err));
        for (int t = 0; t < 50; ++t) s->tick();
        CHECK(s->save().ok);
        const Colony first = s->colony();
        for (int t = 0; t < 50; ++t) s->tick();
        auto r = s->save();
        CHECK(r.ok);
        s.reset();
        {
            std::ofstream f(fd + "/" + r.file, std::ios::binary | std::ios::trunc);
            f << "not a save";
        }
        s = open(fd);
        auto l = s->load();
        std::string why;
        CHECK_MSG(l.ok && l.usedFallback && !l.warnings.empty() && equal(s->colony(), first, &why), l.error + why);
    }
    return check::finish();
}
