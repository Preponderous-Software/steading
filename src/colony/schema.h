// How the colony's components map onto USD names (RFC 0019 §5). The game owns
// the `colony:` namespace; the engine owns `eng:`. These names are the save
// format: renaming one is a schema change that needs a migration and a bump of
// kSchemaVersion.
#pragma once

namespace steading::schema {

constexpr const char* kGameId = "steading";
constexpr int kSchemaVersion = 1;

// Entity collections under /World/Entities and their prim-name prefixes.
constexpr const char* kSettlers = "Settlers";
constexpr const char* kSettlerPrefix = "S_";
constexpr const char* kBuildings = "Buildings";
constexpr const char* kBuildingPrefix = "B_";

// Per-entity fields (int attributes on each entity prim), in column order.
constexpr const char* kSettlerFields[] = {"colony:x",     "colony:y",     "colony:task",
                                          "colony:tx",    "colony:ty",    "colony:carry",
                                          "colony:carryType", "colony:hunger", "colony:patience"};
constexpr const char* kBuildingFields[] = {"colony:x", "colony:y", "colony:builtTick"};

// Grid channels (one int[] per 16x16 chunk prim under /World/Grid).
constexpr const char* kGridChannels[] = {"colony:terrain", "colony:resType", "colony:resAmount",
                                         "colony:designated"};

// Globals (int64 attributes on /World/Globals).
constexpr const char* kGlobals[] = {"colony:tick",          "colony:rng",           "colony:wood",
                                    "colony:stone",         "colony:food",          "colony:stockX",
                                    "colony:stockY",        "colony:maxSettlers",   "colony:nextSettler",
                                    "colony:nextBuilding",  "colony:seed"};

// Seasons are a variant set on the Tree prefab.
constexpr const char* kSeasonPrim = "/Prefabs/Tree";
constexpr const char* kSeasonSet = "season";

}  // namespace steading::schema
