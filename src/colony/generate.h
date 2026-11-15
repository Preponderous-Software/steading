// Seeded generation of a new colony: the content of the immutable base layer.
#pragma once

#include <cstdint>

#include "components.h"

namespace steading {

struct GenParams {
    uint64_t seed = 42;
    int32_t width = 64, height = 64;
    int32_t settlers = 30;
    int32_t maxSettlers = 0;  // 0 means 2 x settlers
};

// Same params -> identical Colony (and therefore a byte-identical base layer).
Colony generate(const GenParams& p);

struct Rng;
// The second half of generation, shared by generate() and the map importer
// (realmap.h): on a filled grid, places the stockpile on the clear grass cell
// nearest the centre, then the settlers around it, and sets the colony-wide
// state. `rng` continues the stream that filled the grid.
void settle(Colony& c, const GenParams& p, Rng& rng);

}  // namespace steading
