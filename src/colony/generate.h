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

}  // namespace steading
