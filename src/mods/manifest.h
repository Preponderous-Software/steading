// Mod manifests and load-order resolution (RFC 0019 §7).
//
// A mod is a directory holding a `mod.toml` manifest and one or more USD layers.
// Mods are data only: they can override prefab stats and looks, add prefabs and
// variants, and override base-world content. They cannot run code or edit saves.
// This file is USD-free; the engine inserts the resolved layers between the
// base layer and the save, in load order.
//
// mod.toml is a strict TOML subset (one key per line, # comments):
//
//   id        = "cheap_huts"            # [a-z0-9_-]{1,64}; unique
//   name      = "Cheap huts"
//   version   = "1.0.0"
//   game      = "steading"
//   schema    = 1                       # the game schema version it targets
//   layers    = ["cheap_huts.usda"]     # weakest first, relative to the mod dir
//   requires  = ["other_mod"]           # optional; must load EARLIER
//   conflicts = ["rival_mod"]           # optional; may not load together
#pragma once

#include <string>
#include <vector>

namespace steading::mods {

struct Manifest {
    std::string id, name, version, game;
    int schema = 0;
    std::vector<std::string> layers;
    std::vector<std::string> required;  // key "requires" (C++20 keyword, hence the name)
    std::vector<std::string> conflicts;
    std::string dir;  // directory the manifest was loaded from ("" when parsed from text)
};

// Parse manifest text. Unknown keys, duplicate keys, malformed values, a bad
// id and unsafe layer paths are all errors.
bool parse(const std::string& text, Manifest& out, std::string* err);
// Read and parse <dir>/mod.toml; sets out.dir.
bool loadDir(const std::string& dir, Manifest& out, std::string* err);

// A layer path is safe when it stays inside the mod directory: relative, no
// "..", no backslashes, no URL scheme, no empty segments, and a USD extension.
bool safeLayerPath(const std::string& path, std::string* why = nullptr);

struct Resolved {
    std::vector<std::string> ids;         // in load order
    std::vector<std::string> layerPaths;  // dir/layer, weakest first (load order)
};

// Check a player's chosen load order (earlier = weaker; later mods win) and
// return the layers to stack. Every problem is reported, not just the first:
// wrong game, wrong schema, duplicates, a missing or later-loaded requirement,
// and conflicting pairs.
bool resolve(const std::vector<Manifest>& order, const std::string& gameId, int schemaVersion, Resolved& out,
             std::vector<std::string>& errors);

}  // namespace steading::mods
