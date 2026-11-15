# Map fixtures

Heightmap world packages for the tests (format: [docs/WORLD_PACKAGE.md](../../docs/WORLD_PACKAGE.md)).

`island/` is a 48 x 48 px window of a synthetic island generated from scratch (a radial island with a lake, a
sand rim, sine-wave forest patches and a stone mountain with a snow cap; no real world's data). Its classes, as
`tests/test_realmap.cpp` counts them: water 50, plain 1315, forest 269, snow 156, sand 143, stone 371. Only
`world.json`'s required and file fields are kept.
