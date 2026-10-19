// History export: recording a replay only reads the colony. The fingerprints a run produces are
// identical with and without recording, at every tick, and the replay carries exactly those
// fingerprints as its state hashes.
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "colony/generate.h"
#include "colony/systems.h"
#include "replay/replay.h"

using namespace steading;

namespace {

std::vector<uint64_t> plain(const GenParams& p, long ticks) {
    Colony c = generate(p);
    Derived d = derive(c);
    std::vector<uint64_t> fp{fingerprint(c)};
    for (long t = 0; t < ticks; ++t) {
        tick(c, d, Stats{});
        fp.push_back(fingerprint(c));
    }
    return fp;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    GenParams p;
    p.seed = 7;
    const long ticks = 400;
    const auto expected = plain(p, ticks);

    for (long every : {1L, 7L, 400L}) {
        char name[64];
        std::snprintf(name, sizeof name, "every %ld: same fingerprints with recording", every);
        SECTION(name);
        Colony c = generate(p);
        Derived d = derive(c);
        replay::Options o;
        o.ticks = ticks;
        o.every = every;
        const std::string path = dir + "/replay-every" + std::to_string(every) + ".usda";
        const auto samples = replay::record(c, d, Stats{}, replay::Colors{}, o, path);
        CHECK(fingerprint(c) == expected.back());
        CHECK(samples.front().tick == 0 && samples.back().tick == ticks);
        CHECK(samples.size() == static_cast<size_t>(ticks / every + 1 + (ticks % every ? 1 : 0)));
        bool all = true;
        for (const auto& s : samples) all = all && s.fingerprint == expected[static_cast<size_t>(s.tick)];
        CHECK_MSG(all, "a sample's fingerprint differs from the run without recording");

        const std::string text = slurp(path);
        CHECK(text.rfind("#usda 1.0", 0) == 0);
        CHECK(text.find("string schema = \"usd-replay/1\"") != std::string::npos);
        CHECK(text.find("string seed = \"7\"") != std::string::npos);
        char hash[40];
        std::snprintf(hash, sizeof hash, "%ld: \"%016" PRIx64 "\"", ticks, expected.back());
        CHECK_MSG(text.find(hash) != std::string::npos, "the last state hash is not in the replay");
        for (const char* prim : {"\"settlers\"", "\"huts\"", "\"trees\"", "\"terrain\""})
            CHECK_MSG(text.find(prim) != std::string::npos, prim);
    }

    SECTION("the same seed gives a byte-identical replay");
    {
        replay::Options o;
        o.ticks = 120;
        o.every = 5;
        Colony a = generate(p), b = generate(p);
        Derived da = derive(a), db = derive(b);
        replay::record(a, da, Stats{}, replay::Colors{}, o, dir + "/replay-a.usda");
        replay::record(b, db, Stats{}, replay::Colors{}, o, dir + "/replay-b.usda");
        CHECK(slurp(dir + "/replay-a.usda") == slurp(dir + "/replay-b.usda"));
    }
    return check::finish();
}
