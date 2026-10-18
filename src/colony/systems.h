// Colony rules: systems that run over the components once per tick, plus the
// player's orders. All of it is deterministic: the same Colony, Stats and order
// sequence always produce the same next Colony, which is what lets a reloaded
// save keep simulating identically to the original run.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "components.h"
#include "rng.h"
#include "stats.h"

namespace steading {

// Caches DERIVED from the saved state. Never saved; rebuilt by derive() after
// every load, so they cannot drift from the save.
struct Derived {
    std::vector<uint8_t> occupied;  // 1 where a building (or plan) stands
};
Derived derive(const Colony& c);

// Advance the colony by one tick.
void tick(Colony& c, Derived& d, const Stats& s);

// The individual systems, in the order tick() runs them. Exposed for tests.
namespace systems {
void needs(Colony& c, std::vector<size_t>& dead);                    // hunger, eating, starvation
void tasks(Colony& c, const Stats& s, Rng& rng);                     // settler state machines
void removeDead(Colony& c, const std::vector<size_t>& dead);
void build(Colony& c, Derived& d, const Stats& s);                   // plans first, then auto-placement
void growth(Colony& c, const Stats& s);                              // births up to the housing cap
void regrowth(Colony& c, const Stats& s, Rng& rng);                  // trees regrow by season
void seasons(Colony& c);                                             // advance tick, flip season
}  // namespace systems

// ---- Player orders --------------------------------------------------------
// Orders change saved state between ticks. They validate their input and
// return false (with a reason) instead of throwing.

// Mark (on=true) or clear every resource tile in the inclusive rectangle as
// designated for gathering. Settlers prefer designated tiles.
int designate(Colony& c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool on);
// Place a hut plan; it is built (wood permitting) before any automatic hut.
bool planHut(Colony& c, Derived& d, int32_t x, int32_t y, std::string* why = nullptr);
// Remove the hut plan at (x, y), if any.
bool cancelPlan(Colony& c, Derived& d, int32_t x, int32_t y);

// ---- Win / lose -----------------------------------------------------------
struct Goals {
    int32_t winters = 5;       // survive this many winters
    int32_t population = 60;   // or grow to this many settlers
};
enum class Outcome { Playing, Won, Lost };
Outcome outcome(const Colony& c, const Goals& g = {});
int64_t wintersSurvived(const Colony& c);
int64_t housingCap(const Colony& c, const Stats& s);

}  // namespace steading
