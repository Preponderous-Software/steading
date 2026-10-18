// Type-level stats. In play they are read once at load from the composed USD
// prefab library (content + mods + season variants), so mods change them; the
// defaults below mirror content/prefabs.usda and let the rules run without USD.
#pragma once

namespace steading {

struct Stats {
    double hutWoodCost = 20;      // /Prefabs/Hut colony:stats:woodCost
    double hutCapacity = 4;       // /Prefabs/Hut colony:stats:capacity
    double regrowSummer = 40;     // /Prefabs/Tree{season=summer} colony:stats:regrowPerMille
    double regrowWinter = 0;      // /Prefabs/Tree{season=winter} colony:stats:regrowPerMille
};

// Fixed rule constants (not moddable yet; RFC 0019 open question 4).
struct Rules {
    static constexpr int kMealEvery = 150;         // hunger at which a settler eats one food
    static constexpr int kStarveAt = 400;          // hunger at which a settler dies
    static constexpr int kCarryMax = 5;            // units a settler can carry
    static constexpr int kSearchRadius = 10;       // tiles searched for a resource
    static constexpr int kGiveUpToResource = 40;   // ticks en route before giving up
    static constexpr int kGiveUpReturning = 120;
    static constexpr int kWanderCooldown = 25;     // ticks of wandering after giving up
    static constexpr int kBirthEvery = 10;         // ticks between births
    static constexpr int kBirthFood = 8;           // food a birth costs
    static constexpr int kBaseCapacity = 4;        // population cap without huts
    static constexpr int kSeasonLength = 100;      // ticks per season
    static constexpr int kTreeAmount = 10, kStoneAmount = 20, kBerryAmount = 8;
};

}  // namespace steading
