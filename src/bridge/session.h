// A playing session: the colony's live state, its save slot and its stats.
#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "colony/generate.h"
#include "colony/looks.h"
#include "colony/systems.h"
#include "mods/manifest.h"
#include "strata/save/slot.h"

namespace steading {

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
