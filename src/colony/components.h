// The colony's runtime state as typed structure-of-arrays components.
//
// This is the game side of RFC 0019's "flat runtime": one std::vector per field,
// one row per entity, rows sorted by a never-reused id. Systems (systems.h) are
// plain loops over these columns; nothing here touches USD. The engine sees this
// state only at sync points (load, save, record), through the schema mapping in
// schema.h, so every field below is saved and must round-trip bit-exactly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace steading {

enum class Terrain : int32_t { Grass = 0, Water = 1, Rock = 2 };
enum class Resource : int32_t { None = 0, Tree = 1, Stone = 2, Berry = 3 };
enum class Task : int32_t { Idle = 0, ToResource = 1, Gather = 2, Return = 3 };
enum class BuildingKind : int32_t { Stockpile = 0, Hut = 1, HutPlan = 2 };
enum class Season : int32_t { Summer = 0, Winter = 1 };

// Settlers: small state machines (idle -> walk to resource -> gather -> carry home).
struct Settlers {
    std::vector<int64_t> id;  // sorted ascending, never reused
    std::vector<int32_t> x, y;
    std::vector<int32_t> task;       // Task
    std::vector<int32_t> tx, ty;     // current target cell
    std::vector<int32_t> carry;      // units carried
    std::vector<int32_t> carryType;  // Resource
    std::vector<int32_t> hunger;     // ticks since last meal
    std::vector<int32_t> patience;   // >0: ticks en route; <0: wandering cooldown

    size_t size() const { return id.size(); }
    // Appends a settler at (x, y) with every other field zeroed. `newId` must be
    // greater than every existing id. Returns the row.
    size_t add(int64_t newId, int32_t px, int32_t py);
    void remove(size_t row);  // stable erase
};

// Buildings: the stockpile, huts and player-placed hut plans.
struct Buildings {
    std::vector<int64_t> id;  // sorted ascending, never reused
    std::vector<int32_t> kind;  // BuildingKind
    std::vector<int32_t> x, y;
    std::vector<int32_t> builtTick;  // tick the building was completed (plans: tick placed)

    size_t size() const { return id.size(); }
    size_t add(int64_t newId, BuildingKind k, int32_t px, int32_t py, int32_t tick);
    void remove(size_t row);
};

// The tile grid, one int32 channel per property, row-major (y * w + x).
struct Grid {
    int32_t w = 0, h = 0;
    std::vector<int32_t> terrain;     // Terrain
    std::vector<int32_t> resType;     // Resource
    std::vector<int32_t> resAmount;   // units left on the tile
    std::vector<int32_t> designated;  // 1 = the player marked this tile for gathering

    void init(int32_t width, int32_t height);
    bool inBounds(int32_t px, int32_t py) const { return px >= 0 && py >= 0 && px < w && py < h; }
    size_t at(int32_t px, int32_t py) const { return static_cast<size_t>(py) * static_cast<size_t>(w) + px; }
    size_t cells() const { return static_cast<size_t>(w) * static_cast<size_t>(h); }
};

// Colony-wide scalar state. Every field is saved.
struct Globals {
    int64_t tick = 0;
    int64_t rng = 0;  // Rng::state between ticks
    int64_t wood = 0, stone = 0, food = 0;
    int64_t stockX = 0, stockY = 0;
    int64_t maxSettlers = 0;
    int64_t nextSettlerId = 0, nextBuildingId = 0;
    int64_t seed = 0;
};

// The whole saved state of a colony.
struct Colony {
    Grid grid;
    Settlers settlers;
    Buildings buildings;
    Globals globals;
    Season season = Season::Summer;  // saved as a variant selection on the Tree prefab
};

// Exact, field-by-field comparison. On a difference returns false and, if
// `why` is given, describes the first differing field.
bool equal(const Colony& a, const Colony& b, std::string* why = nullptr);

// 64-bit FNV-1a over every saved field in a fixed order: a cheap fingerprint
// for determinism checks (not a save checksum; saves use Strata's SHA-256).
uint64_t fingerprint(const Colony& c);

}  // namespace steading
