# Save fixtures

Each directory here is a real save slot written by the game, exactly as a player's `--slot` directory
looks (the save core's empty `LOCK` file is left out). They are **never edited or regenerated**: they
stand for saves players already have, and `tests/test_save_fixtures.cpp` loads every one of them on
each CI run. A change to the save format gets a new `kSchemaVersion` (`src/colony/schema.h`), a
migration, and a new `v<N>-…` fixture written by the game at that version; the `v1-` ones stay.

Fixture names are `v<schema version>-<what>-tick<N>`. The `.gitattributes` here keeps every byte as
written (the slot's `HEAD` carries a checksum).

## Provenance

Both were written on 2026-10-24 by the game built from `main` at
`adb087c56472ff53d03d2b4bb7713df981e30ac9` (steading 0.1.0, never released, so this is
`kSchemaVersion` 1), with Strata at `strata.ref` (`c2d7ee4c1102726c654c90d8361383d9ba3590b3`) and
Strata's OpenUSD prebuilt `openusd-v26.08-ubuntu24.04-gcc13-cdd5cf8adfa8`. The build ran in an
`ubuntu:24.04` root (GCC 13.3, CMake 3.28) configured as ci.yml's `game` job does, and the game ran
headless on Mesa llvmpipe:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEADING_WARNINGS_AS_ERRORS=ON \
  -DSTEADING_WITH_STRATA=ON -DFETCHCONTENT_SOURCE_DIR_STRATA=<strata checkout> \
  -DCMAKE_PREFIX_PATH=<openusd prebuilt> -Wno-dev
cmake --build build

# v1-seed42-tick500: ci.yml's headless smoke, a new colony saved at tick 400, then continued and saved at 500
build/steading --slot v1-seed42-tick500 --new 42 --headless --ticks 400
#   tick 400 summer | settlers 48/80 | wood 5 stone 40 food 0 | winters 2 | playing (saved)
build/steading --slot v1-seed42-tick500 --headless --ticks 100
#   tick 500 winter | settlers 49/80 | wood 5 stone 40 food 6 | winters 2 | playing (saved)

# v1-frost-huts-seed7-tick600: a colony with the frost_huts mod in its load order
build/steading --slot v1-frost-huts-seed7-tick600 --new 7 --mods frost_huts --headless --ticks 600
#   tick 600 summer | settlers 26/52 | wood 0 stone 0 food 0 | winters 3 | playing (saved)
```

The seed-42 slot holds two saves (ticks 400 and 500), so the fallback to the previous save is tested
on a real slot too. SHA-256 of every file as written:

```
199f57def085890ee01fed3c0118513997bd5cf489ce9393998e7ba1c7fd4906  v1-seed42-tick500/HEAD
32db40585f0a2f9a8f0e07022f2870cbcf0b4b11d4454f52d0fcfd055e7195f5  v1-seed42-tick500/HEAD.prev
a39853a1dac7b20ebaecb4918ad14346d4fb79e8042bc21fa609368ca9edf0b4  v1-seed42-tick500/base-a39853a1dac7b20ebaecb4918ad14346d4fb79e8042bc21fa609368ca9edf0b4.usdc
18fc7909b81c40d2b4329e0b4a3b34f33ff72f0ad89279c161312fa68c01f84a  v1-seed42-tick500/save-000000000400-18fc7909b81c40d2b4329e0b4a3b34f33ff72f0ad89279c161312fa68c01f84a.usdc
1e170a6161cce55b884ff0d3d6710da990a699bda091def507d14e561100b1d1  v1-seed42-tick500/save-000000000500-1e170a6161cce55b884ff0d3d6710da990a699bda091def507d14e561100b1d1.usdc
0499145b116051112d9366210d38ef1441c16238cd53766c3d2c14e0b6022b2e  v1-frost-huts-seed7-tick600/HEAD
b74567e736658aff04c97b846bb984a4d752e9c2332a3c87864ca26d0cdba923  v1-frost-huts-seed7-tick600/HEAD.prev
cf5c7049bc85f7d193adf92ecf582458dd2e1b62d8e12b9a42fc08683776deb4  v1-frost-huts-seed7-tick600/base-cf5c7049bc85f7d193adf92ecf582458dd2e1b62d8e12b9a42fc08683776deb4.usdc
e702fea0486fcc0065684fa09ca253182e941b4ab3cfc316de56463f368c35a7  v1-frost-huts-seed7-tick600/save-000000000600-e702fea0486fcc0065684fa09ca253182e941b4ab3cfc316de56463f368c35a7.usdc
```
