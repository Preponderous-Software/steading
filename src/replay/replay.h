// History export: run a colony forward and record it as a usd-replay/1 replay.
//
// The replay contract (usd-recorder's docs/REPLAY.md) is a small fixed USD layout any replay
// player can show: one Points prim per kind of thing (settlers, huts, trees, ...) sampled every
// N ticks, the terrain as a colour grid, the colony's counters as stats, and the colony's own
// state fingerprint (fingerprint() in components.h) at every sample. Recording only READS the
// colony, so a run's fingerprints are the same with and without it (tests/test_replay.cpp).
//
// This never reads or writes a save: the caller hands over a Colony (generated from a seed, or
// loaded from a COPY of a save slot by app/replay.cpp) and gets a .usda text file back.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "colony/components.h"
#include "colony/stats.h"
#include "colony/systems.h"

namespace steading::replay {

// Display colours (linear 0-1, as the game's Looks): the base content's prefab colours by
// default; a caller with composed content (mods) passes those instead.
struct Colors {
    float settler[3] = {0.95f, 0.75f, 0.55f};
    float hut[3] = {0.55f, 0.35f, 0.2f};
    float hutPlan[3] = {0.8f, 0.7f, 0.55f};
    float stockpile[3] = {0.9f, 0.8f, 0.2f};
    float treeSummer[3] = {0.1f, 0.45f, 0.12f};
    float treeWinter[3] = {0.85f, 0.9f, 0.95f};
};

struct Options {
    long ticks = 1000;           // ticks to run after the first sample
    long every = 4;              // one sample every N ticks (and always the last tick)
    double playbackRate = 40;    // suggested ticks per second of playback
    std::string simVersion = "dev";
    std::string title, source;   // optional metadata
    std::vector<std::string> mods;  // load order, recorded in the metadata
};

struct Sample {
    int64_t tick;
    uint64_t fingerprint;
};

// Run `c` forward `o.ticks` ticks with the rules, writing a usd-replay/1 .usda to `path`.
// Returns the (tick, fingerprint) of every sample. Throws std::exception on a write error.
std::vector<Sample> record(Colony& c, Derived& d, const Stats& s, const Colors& colors, const Options& o,
                           const std::string& path);

}  // namespace steading::replay
