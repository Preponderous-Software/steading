# Heightmap world packages

`steading --map DIR` (and `steading-sim --map DIR`) starts a new colony on a real map instead of a seeded one. The
map is a **heightmap world package**: a folder holding `world.json`, `height.png` and `ground.png`. One package
pixel becomes one tile, so a package is at most 256 x 256 px (`kMaxMapSide` in `src/colony/realmap.h`); cut a
larger map down to the area you want to settle first.

```sh
steading --map tests/maps/island                 # a new colony on the bundled synthetic island, seed 42
steading --map path/to/package --new 7           # the seed only scatters resources and places the settlers
steading-sim --map tests/maps/island --ticks 1000
```

The package is read once, when the colony is made. The save holds the whole grid, so a map colony continues and
reloads without its package, exactly like a seeded one.

## world.json

```json
{
  "grid": {"origin_x": 588, "origin_z": 284, "blocks_per_px": 4, "width": 48, "height": 48, "y_offset": 1024},
  "classes": ["void", "water", "plain", "forest", "snow", "sand", "stone", "badland", "ice", "lava", "built", "farm"],
  "files": {"height": "height.png", "ground": "ground.png"}
}
```

| Field | Required | Meaning |
|---|---|---|
| `grid.width`, `grid.height` | yes | raster size in pixels (1 to 256 each for steading) |
| `classes` | yes | ground class names; `ground.png` holds indices into this list (1 to 256 names) |
| `files.height`, `files.ground` | no | raster file names inside the package (default `height.png`, `ground.png`); a name with a path separator is refused |
| `grid.origin_x`, `grid.origin_z`, `grid.blocks_per_px` | no | where the pixels sit in the source world: pixel (row i, col j) covers x in [origin_x + j * blocks_per_px, + blocks_per_px), z in [origin_z + i * blocks_per_px, + blocks_per_px). x is east, z is south, row 0 is north |
| `grid.y_offset` | no | added to every height before it is stored (so negative heights fit an unsigned PNG) |

Other fields are allowed and ignored.

## Rasters

| File | Type | Meaning |
|---|---|---|
| `height.png` | 16-bit greyscale PNG | surface height + `grid.y_offset`; **0 = no data** |
| `ground.png` | 8-bit greyscale PNG | index into `classes` |

Both must be `grid.width` x `grid.height`. A ground index outside `classes` is refused.

## How steading reads it

| Ground class | Tile |
|---|---|
| `water`, `ice` | Water |
| `stone`, `snow`, `lava` | Rock |
| `forest` | Grass, with a tree on about half of the tiles (so settlers can walk through) |
| `plain`, `sand`, `badland`, `farm`, `built`, and any class steading does not know | Grass |
| `void`, or no data in `height.png` | Water |

Then, with the seed: stone on about 30% of the Rock tiles within two tiles of Grass (a mountain's flanks, not its
heart), berries on 2.5% of the open Grass, the stockpile on the clear Grass tile nearest the centre, and the settlers
around it, exactly as seeded generation does (`settle()` in `src/colony/generate.cpp` is shared). The same package and
seed always give the same colony; seeded generation is unchanged (its pinned fingerprints stand).

steading uses the heights only to find pixels with no data: the grid is flat.

## The bundled fixture

`tests/maps/island` is a 48 x 48 px package cut from a synthetic island (sea, sand rim, grassland with forest patches,
a stone mountain with a snow cap), built from scratch for the tests: it is no one's world. `tests/test_realmap.cpp`
checks the reader against it, and `tests/test_saves.cpp` saves and reloads a colony made from it.
