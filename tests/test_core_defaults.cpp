// The browser build has no OpenUSD, so it draws and plays with the built-in
// Stats defaults (colony/stats.h) and coreLooks() (colony/looks.h) instead of
// reading content/prefabs.usda. This test (built only with OpenUSD) checks
// that those are exactly what the desktop game reads from the core
// content with no mods, so both builds play the same rules in the same colours.
#include <cstdio>
#include <filesystem>
#include <string>

#include "bridge/session.h"
#include "check.h"

using namespace steading;

namespace {

bool same3(const float a[3], const float b[3]) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

}  // namespace

int main(int argc, char** argv) {
    const std::string dir =
        argc > 1 ? argv[1] : (std::filesystem::temp_directory_path() / "steading-core-defaults").u8string();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::u8path(dir), ec);
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);

    SECTION("core content (no mods) composes to the built-in Stats and Looks");
    std::string err;
    auto s = Session::open({STEADING_SOURCE_DIR, dir, {}}, &err);
    CHECK_MSG(s != nullptr, err);
    if (s) {
        const Stats d;
        CHECK(s->stats().hutWoodCost == d.hutWoodCost);
        CHECK(s->stats().hutCapacity == d.hutCapacity);
        CHECK(s->stats().regrowSummer == d.regrowSummer);
        CHECK(s->stats().regrowWinter == d.regrowWinter);
        const Looks l = coreLooks();
        const Looks& r = s->looks();
        CHECK(same3(r.settler, l.settler));
        CHECK(same3(r.hut, l.hut));
        CHECK(same3(r.hutPlan, l.hutPlan));
        CHECK(same3(r.stockpile, l.stockpile));
        CHECK(same3(r.treeSummer, l.treeSummer));
        CHECK(same3(r.treeWinter, l.treeWinter));
    }

    SECTION("the winter variant repaints trees (a local colour on the Geom would outvote it)");
    if (s) {
        const Looks d;
        CHECK(same3(s->looks().treeSummer, d.treeSummer));
        CHECK(same3(s->looks().treeWinter, d.treeWinter));
        CHECK(!same3(s->looks().treeSummer, s->looks().treeWinter));
    }
    return check::finish();
}
