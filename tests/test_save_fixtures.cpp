// Released saves keep loading. Every slot under tests/fixtures/ was written by
// the real game (tests/fixtures/README.md says by which commit and command)
// and is never edited: a change that stops one of them loading breaks saves
// players already have. Each fixture is loaded from a copy (loading takes the
// slot's lock), checked against what was recorded when it was written, played
// on, saved and reloaded exactly. The seed-42 slot also keeps its previous
// save, which must take over when the latest save is damaged.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bridge/session.h"
#include "check.h"
#include "colony/schema.h"
#include "colony/systems.h"

using namespace steading;
namespace fs = std::filesystem;

static_assert(schema::kSchemaVersion == 1,
              "a new save format needs its own v<N>- fixture in tests/fixtures (README.md there)");

namespace {

const std::string kGame = STEADING_SOURCE_DIR;
const std::string kFixtures = STEADING_SOURCE_DIR "/tests/fixtures";
std::string root;

// A fresh copy of fixture `name`: every save file, never the lock.
std::string copyOf(const std::string& name) {
    const fs::path to = fs::u8path(root) / name;
    std::error_code ec;
    fs::remove_all(to, ec);
    fs::create_directories(to);
    for (const auto& e : fs::directory_iterator(fs::u8path(kFixtures + "/" + name)))
        if (e.is_regular_file() && e.path().filename() != "LOCK") fs::copy_file(e.path(), to / e.path().filename());
    return to.u8string();
}

std::unique_ptr<Session> open(const std::string& dir, std::vector<std::string> mods = {}) {
    std::string err;
    auto s = Session::open({kGame, dir, std::move(mods)}, &err);
    if (!s) std::printf("  open failed: %s\n", err.c_str());
    return s;
}

struct Expect {
    const char* name;
    std::vector<std::string> mods;
    int64_t tick;
    uint64_t seed;
    size_t settlers;
    int64_t winters;
};

}  // namespace

int main(int argc, char** argv) {
    root = argc > 1 ? argv[1] : (fs::temp_directory_path() / "steading-fixture-tests").u8string();
    std::error_code ec;
    fs::create_directories(fs::u8path(root), ec);

    // What the game printed when it wrote each fixture (tests/fixtures/README.md).
    const Expect fixtures[] = {
        {"v1-seed42-tick500", {}, 500, 42, 49, 2},
        {"v1-frost-huts-seed7-tick600", {"frost_huts"}, 600, 7, 26, 3},
    };
    for (const Expect& x : fixtures) {
        SECTION((std::string(x.name) + ": loads, plays on, saves and reloads exactly").c_str());
        const std::string dir = copyOf(x.name);
        auto s = open(dir, x.mods);
        CHECK(s != nullptr);
        if (!s) continue;
        auto l = s->load();
        CHECK_MSG(l.ok, l.error);
        std::string notes;
        for (const auto& w : l.warnings) notes += " warning: " + w;
        for (const auto& r : l.rejected) notes += " rejected: " + r;
        CHECK_MSG(!l.usedFallback && !l.migrated && l.warnings.empty(),
                  std::string(x.name) + ": the latest save itself must load, unmigrated;" + notes);
        const Colony& c = s->colony();
        CHECK(c.globals.tick == x.tick);
        CHECK(c.globals.seed == static_cast<int64_t>(x.seed));
        CHECK_MSG(c.settlers.size() == x.settlers, std::to_string(c.settlers.size()) + " settlers");
        CHECK(wintersSurvived(c) == x.winters);
        for (int t = 0; t < 100; ++t) s->tick();
        const Colony live = s->colony();
        auto r = s->save();
        CHECK_MSG(r.ok, r.error);
        auto again = s->load();
        std::string why;
        CHECK_MSG(again.ok && equal(s->colony(), live, &why), again.error + why);
        CHECK(s->colony().globals.tick == x.tick + 100);
    }

    SECTION("v1-frost-huts-seed7-tick600: refused without its mod");
    {
        auto s = open(copyOf("v1-frost-huts-seed7-tick600"));
        auto l = s ? s->load() : strata::save::LoadResult{};
        CHECK_MSG(!l.ok && l.error.find("mod list differs") != std::string::npos, l.error);
    }

    SECTION("v1-seed42-tick500: a damaged latest save gives way to the tick-400 save");
    {
        const std::string dir = copyOf("v1-seed42-tick500");
        for (const auto& e : fs::directory_iterator(fs::u8path(dir)))
            if (e.path().filename().u8string().rfind("save-000000000500-", 0) == 0) {
                std::ofstream f(e.path(), std::ios::binary | std::ios::trunc);
                f << "not a save";
            }
        auto s = open(dir);
        auto l = s ? s->load() : strata::save::LoadResult{};
        CHECK_MSG(l.ok && l.usedFallback && !l.warnings.empty(), l.error);
        if (s) CHECK(s->colony().globals.tick == 400);
    }
    return check::finish();
}
