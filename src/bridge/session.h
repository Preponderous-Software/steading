// A playing session: the colony's live state, its save slot and its stats.
#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "colony/generate.h"
#include "colony/systems.h"
#include "mods/manifest.h"
#include "strata/save/slot.h"

namespace steading {

// Colours read from the composed prefabs (content + mods + season variants):
// what the renderer draws. A mod that repaints huts repaints them on screen.
struct Looks {
    float settler[3] = {0.95f, 0.75f, 0.55f};
    float hut[3] = {0.55f, 0.35f, 0.2f};
    float hutPlan[3] = {0.8f, 0.7f, 0.55f};
    float stockpile[3] = {0.9f, 0.8f, 0.2f};
    float treeSummer[3] = {0.1f, 0.45f, 0.12f};
    float treeWinter[3] = {0.85f, 0.9f, 0.95f};
};

class Session {
public:
    struct Options {
        std::string gameDir;                // holds content/ and mods/
        std::string slotDir;                // the save slot
        std::vector<std::string> modOrder;  // mod ids, load order (later wins)
    };
    static std::unique_ptr<Session> open(const Options& opt, std::string* err);

    bool hasGame() const;                                   // the slot has a HEAD
    bool newGame(const GenParams& p, std::string* err);     // refuses if the slot has a game
    strata::save::LoadResult load(const strata::save::LoadOptions& opt = {});
    strata::save::SaveResult save();                        // synchronous
    std::future<strata::save::SaveResult> saveAsync();      // snapshot here, write on a worker

    void tick() { steading::tick(colony_, derived_, stats_); }
    Colony& colony() { return colony_; }
    Derived& derived() { return derived_; }
    const Stats& stats() const { return stats_; }
    const Looks& looks() const { return looks_; }
    const std::vector<std::string>& modIds() const { return modIds_; }

private:
    Session() = default;
    Options opt_;
    std::unique_ptr<strata::save::Slot> slot_;
    std::vector<std::string> modIds_;
    std::vector<strata::save::ModRef> mods_;
    std::vector<std::string> content_;
    Colony colony_;
    Derived derived_;
    Stats stats_;
    Looks looks_;
};

// All mods found under <gameDir>/mods/*/mod.toml, by id.
std::vector<mods::Manifest> discoverMods(const std::string& gameDir, std::vector<std::string>* errors = nullptr);

}  // namespace steading
