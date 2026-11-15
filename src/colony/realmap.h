// Starting a colony on a real map: a heightmap world package instead of noise.
//
// A world package is a folder (the format is in docs/WORLD_PACKAGE.md):
//
//   world.json   grid size, the ground class names and the raster file names
//   height.png   16-bit grey: surface height + grid.y_offset; 0 = no data
//   ground.png   8-bit grey: an index into world.json's `classes`
//
// One package pixel becomes one tile. Ground classes map onto steading's
// terrain (water and ice are Water; stone, snow and lava are Rock; everything
// else is Grass, with trees on forest) and pixels with no data become Water.
// Resources are scattered with the seed, so the same package and seed always
// give the same colony. The package is read once: a save holds the whole grid,
// so a map colony loads without its package.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "components.h"
#include "generate.h"

namespace steading {

// The biggest package side importMap accepts, in pixels (crop larger maps first).
constexpr int32_t kMaxMapSide = 256;

struct MapPackage {
    int32_t w = 0, h = 0;
    std::vector<std::string> classes;  // ground class names, by index
    std::vector<uint8_t> ground;       // w * h class indices, row-major (row 0 = north)
    std::vector<uint8_t> hasData;      // w * h: 1 where the height raster has a value
};

// Reads world.json, height.png and ground.png from `dir`. On failure returns
// false and describes the problem in `err`.
bool readMapPackage(const std::string& dir, MapPackage& out, std::string* err);

// The terrain a ground class name maps to; `tree` is set for forest.
Terrain terrainForClass(const std::string& name, bool* tree = nullptr);

// A new colony on the package's ground. Uses p.seed, p.settlers and
// p.maxSettlers; the map size comes from the package (p.width and p.height are
// ignored). Throws std::invalid_argument for a package smaller than 8x8, larger
// than kMaxMapSide, or with no grass to settle on.
Colony importMap(const MapPackage& m, const GenParams& p);

}  // namespace steading
