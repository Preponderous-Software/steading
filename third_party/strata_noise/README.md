# Strata noise (vendored)

`include/strata/sim/noise.hpp` and `src/noise.cpp` are copied unchanged from
[Strata](https://github.com/Preponderous-Software/strata) `main` at commit
`649f0a52a70937c5629251457ab7a17c04c05351` (Strata PR #103, after v0.7.0 and not yet in a
release), `modules/sim/include/strata/sim/noise.hpp` and `modules/sim/src/noise.cpp`: seeded 2D
value noise and fractal Brownian motion, deterministic on every compiler when built with
`-ffp-contract=off`. MIT licensed, see [LICENSE](LICENSE).

They are vendored rather than linked from Strata's `strata::sim` module so the rules library
keeps building without Strata (and the browser build, which turns Strata's sim module off,
keeps working), and so the game does not have to pin an unreleased Strata commit. The
determinism test checks the copy against the values Strata's own unit test pins. Update it by
copying the files from a newer Strata commit and bumping this note; once a Strata release
carries `noise.hpp`, linking `strata::sim` instead is an option.
