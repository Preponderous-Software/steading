// Deterministic random numbers and value noise for steading.
//
// Everything here is pure integer/double arithmetic with fixed constants, so the
// same seed produces the same world and the same simulation on every platform.
// The RNG state is part of the saved world (Globals::rng); never use std::rand
// or <random> distributions in game code (their output is implementation-defined).
#pragma once

#include <cmath>
#include <cstdint>

namespace steading {

// SplitMix64 finaliser: a good 64-bit mixing function.
constexpr uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// 64-bit LCG whose output is passed through mix64. The whole state is one
// uint64_t, which the world stores as a global between ticks.
struct Rng {
    uint64_t state;

    uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return mix64(state);
    }
    // Uniform-ish integer in [0, n). n must be > 0. (Modulo bias is irrelevant
    // for gameplay and keeping it makes the sequence trivially reproducible.)
    int below(int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }
    // Integer in [lo, hi] inclusive.
    int range(int lo, int hi) { return lo + below(hi - lo + 1); }
};

// Smooth 2D value noise in [0, 1).
inline double valueNoise(uint64_t seed, double x, double y) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const double fx = x - xi;
    const double fy = y - yi;
    auto corner = [&](int a, int b) {
        const uint64_t h = mix64(seed ^ mix64(static_cast<uint64_t>(static_cast<uint32_t>(a)) * 73856093ULL ^
                                              static_cast<uint64_t>(static_cast<uint32_t>(b)) * 19349663ULL));
        return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
    };
    auto smooth = [](double t) { return t * t * (3 - 2 * t); };
    const double top = corner(xi, yi) + (corner(xi + 1, yi) - corner(xi, yi)) * smooth(fx);
    const double bottom = corner(xi, yi + 1) + (corner(xi + 1, yi + 1) - corner(xi, yi + 1)) * smooth(fx);
    return top + (bottom - top) * smooth(fy);
}

}  // namespace steading
