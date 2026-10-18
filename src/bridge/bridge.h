// steading <-> Strata: the colony's components at the engine's sync points.
//
// The colony ticks its own typed components (src/colony). At load and save it
// converts them to and from Strata's flat world (strata::world::World) with the
// names in schema.h, and Strata's save core does the rest: USD composition,
// checksums, verify-before-commit, fallback, migrations, async saves.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "colony/components.h"
#include "colony/stats.h"
#include "mods/manifest.h"
#include "strata/save/slot.h"
#include "strata/world/world.h"

namespace steading::bridge {

// The empty Strata world for the current schema.
strata::world::World schemaWorld();
// Components -> Strata world. Lossless; this is the save snapshot.
strata::world::World toWorld(const Colony& c);
// Strata world -> components. Validates enum ranges and sizes; false (with a
// reason) on anything the rules could not run on.
bool fromWorld(const strata::world::World& w, Colony& out, std::string* err);

// What Strata needs to know about steading's saves.
strata::save::Schema saveSchema();

// Paths of the game's content (the prefab library), weakest first.
std::vector<std::string> contentLayers(const std::string& gameDir);
// Resolved mod load order -> Strata mod refs.
std::vector<strata::save::ModRef> modRefs(const std::vector<mods::Manifest>& order);

// Type-level stats read through composition (content + mods; seasons through
// the Tree prefab's variant set). False if a layer is missing or unreadable.
bool readStats(const std::vector<std::string>& content, const std::vector<strata::save::ModRef>& mods, Stats& out,
               std::string* err);

}  // namespace steading::bridge
