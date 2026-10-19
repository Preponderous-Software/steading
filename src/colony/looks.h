// What the renderer draws each kind of thing in. USD-free, so a build without
// OpenUSD (the browser build) can draw the colony.
#pragma once

namespace steading {

// Colours read from the composed prefabs (content + mods + season variants):
// what the renderer draws. A mod that repaints huts repaints them on screen.
// The defaults mirror content/prefabs.usda (tests/test_core_defaults.cpp checks that
// they match what the session reads with no mods).
struct Looks {
    float settler[3] = {0.95f, 0.75f, 0.55f};
    float hut[3] = {0.55f, 0.35f, 0.2f};
    float hutPlan[3] = {0.8f, 0.7f, 0.55f};
    float stockpile[3] = {0.9f, 0.8f, 0.2f};
    float treeSummer[3] = {0.1f, 0.45f, 0.12f};
    float treeWinter[3] = {0.85f, 0.9f, 0.95f};
};

}  // namespace steading
