// steading-replay: record a colony's history as a usd-replay/1 replay (.usda).
//
//   steading-replay --new SEED [--size N] [--settlers N] [--ticks N] [--every N] --out run.usda
//   steading-replay --slot DIR [--game-dir DIR] [--mods a,b] [--ticks N] [--every N] --out run.usda
//
// --new generates a colony from a seed (no save is involved). --slot (only in the Strata build)
// starts from a save, READ-ONLY: the slot's files are copied to a temporary directory and the
// copy is loaded through the save core, so nothing in DIR is written, not even its lock file.
// Either way the colony then runs forward with the rules and nothing is saved. Convert the text
// replay to compact binary with `usd-recorder replay convert run.usda run.usdc`.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>

#include "colony/generate.h"
#include "replay/replay.h"

#ifdef STEADING_REPLAY_WITH_SAVES
#include <filesystem>
#include <random>
#include <system_error>
#include <vector>

#include "bridge/session.h"
#endif

#ifndef STEADING_GAME_DIR
#define STEADING_GAME_DIR "."
#endif

using namespace steading;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: steading-replay (--new SEED [--size N] [--settlers N] | --slot DIR [--game-dir DIR] [--mods a,b])\n"
                 "                       [--ticks N] [--every N] [--version V] --out run.usda\n");
    return 2;
}

#ifdef STEADING_REPLAY_WITH_SAVES
namespace fs = std::filesystem;

void removeDir(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(fs::u8path(dir), ec);
}

// Copy the save files of `from` (every regular file but the save core's LOCK) into a fresh
// temporary directory; returns its path ("" on error, with nothing left behind).
std::string copySlot(const std::string& from, std::string* err) {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) {
        *err = "cannot find the temporary directory";
        return "";
    }
    std::random_device rd;
    fs::path tmp;
    for (int tries = 0;; ++tries) {
        char name[32];
        std::snprintf(name, sizeof name, "steading-replay-%08x", static_cast<unsigned>(rd()));
        tmp = base / name;
        if (fs::create_directory(tmp, ec)) break;
        if (tries == 100) {
            *err = "cannot create a temporary directory";
            return "";
        }
    }
    const std::string tmpl = tmp.u8string();
    fs::directory_iterator it(fs::u8path(from), ec);
    if (ec) {
        removeDir(tmpl);
        *err = "cannot open slot " + from;
        return "";
    }
    for (const fs::directory_entry& e : it) {
        if (e.path().filename() == "LOCK" || !e.is_regular_file(ec)) continue;
        if (!fs::copy_file(e.path(), tmp / e.path().filename(), ec)) {
            removeDir(tmpl);
            *err = "cannot copy " + e.path().u8string();
            return "";
        }
    }
    return tmpl;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    GenParams gen;
    bool fresh = false;
    std::string slot, gameDir = STEADING_GAME_DIR, out;
    std::vector<std::string> mods;
    replay::Options opt;
    opt.source = "steading-replay";
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (k == "--new") { fresh = true; gen.seed = std::strtoull(val().c_str(), nullptr, 10); }
        else if (k == "--size") gen.width = gen.height = std::atoi(val().c_str());
        else if (k == "--settlers") gen.settlers = std::atoi(val().c_str());
        else if (k == "--slot") slot = val();
        else if (k == "--game-dir") gameDir = val();
        else if (k == "--mods") {
            std::stringstream ss(val());
            std::string id;
            while (std::getline(ss, id, ',')) if (!id.empty()) mods.push_back(id);
        }
        else if (k == "--ticks") opt.ticks = std::atol(val().c_str());
        else if (k == "--every") opt.every = std::atol(val().c_str());
        else if (k == "--version") opt.simVersion = val();
        else if (k == "--title") opt.title = val();
        else if (k == "--out") out = val();
        else return usage();
    }
    if (out.empty() || fresh == !slot.empty() || opt.ticks < 0 || opt.every < 1) return usage();
    std::vector<replay::Sample> samples;
    try {
        if (fresh) {
            char src[96];
            std::snprintf(src, sizeof src, "steading-replay --new %" PRIu64 " --size %d --settlers %d", gen.seed,
                          gen.width, gen.settlers);
            opt.source = src;
            Colony c = generate(gen);
            Derived d = derive(c);
            samples = replay::record(c, d, Stats{}, replay::Colors{}, opt, out);
        } else {
#ifdef STEADING_REPLAY_WITH_SAVES
            std::string err;
            const std::string copy = copySlot(slot, &err);
            if (copy.empty()) {
                std::fprintf(stderr, "steading-replay: %s\n", err.c_str());
                return 1;
            }
            auto session = Session::open({gameDir, copy, mods}, &err);
            if (!session || !session->hasGame()) {
                removeDir(copy);
                std::fprintf(stderr, "steading-replay: %s\n", session ? "the slot has no save" : err.c_str());
                return 1;
            }
            auto r = session->load();
            if (!r.ok) {
                removeDir(copy);
                std::fprintf(stderr, "steading-replay: cannot load %s: %s\n", slot.c_str(), r.error.c_str());
                return 1;
            }
            replay::Colors colors;
            const Looks& l = session->looks();
            for (int i = 0; i < 3; ++i) {
                colors.settler[i] = l.settler[i];
                colors.hut[i] = l.hut[i];
                colors.hutPlan[i] = l.hutPlan[i];
                colors.stockpile[i] = l.stockpile[i];
                colors.treeSummer[i] = l.treeSummer[i];
                colors.treeWinter[i] = l.treeWinter[i];
            }
            opt.mods = session->modIds();
            opt.source = "steading-replay --slot (a read-only copy of a save, " + r.saveFile + ")";
            samples = replay::record(session->colony(), session->derived(), session->stats(), colors, opt, out);
            session.reset();
            removeDir(copy);
#else
            std::fprintf(stderr, "steading-replay: --slot needs the Strata build (-DSTEADING_WITH_STRATA=ON)\n");
            return 2;
#endif
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "steading-replay: %s\n", e.what());
        return 1;
    }
    std::printf("{\"out\":\"%s\",\"samples\":%zu,\"firstTick\":%" PRId64 ",\"lastTick\":%" PRId64
                ",\"fingerprint\":\"%016" PRIx64 "\"}\n",
                out.c_str(), samples.size(), samples.front().tick, samples.back().tick, samples.back().fingerprint);
    return 0;
}
