# steading in a browser

steading has a browser build at **https://steading.play.danielstephenson.dev**, one of the games on
[danielstephenson.dev/play](https://danielstephenson.dev/play). It plays the same rules as the desktop game,
drawn by Strata's WebGL 2 backend (Strata `docs/WEB.md`).

## What differs from the desktop game

| | Desktop (`STEADING_WITH_STRATA`) | Browser (`STEADING_WEB`) |
|---|---|---|
| Rules (`src/colony`) | the same code | the same code; CI checks the pinned determinism fingerprint `011ed2b2f364b21b` in wasm under Node (ctest) and in Chromium (smoke test) |
| OpenUSD | yes (Strata `modules/usd`, `modules/save`) | **no**: OpenUSD is not built for WebAssembly |
| Saves | Strata's save core (checksummed, verified, fallback, migrations) | **none**. A colony lasts until the tab is closed or reloaded, and the page says so |
| Content | `content/prefabs.usda` + mods, composed through USD | core content only: the built-in `Stats` (`src/colony/stats.h`) and `Looks` (`src/colony/looks.h`) defaults. `tests/test_core_defaults.cpp` (desktop CI) checks that they equal the USD composition with no mods |
| Mods | `--mods a,b` | not available |
| Entry point | `app/main.cpp` | `app/web_main.cpp` (the browser runs the frame loop) |
| Scene, cameras, picking | `app/scene.*` | the same files |

### Why the browser build has no saves

The save format is the desktop game's: content-addressed `.usdc` deltas through Strata's save core, which needs
OpenUSD. The browser build does not write a second, simpler save format next to it. A second format would be a
second thing to keep compatible, and nothing written in a browser could reach a desktop slot. So the browser build
links nothing from `src/bridge` or Strata's save module and writes nothing at all. Browser saves, if wanted later,
are a separate decision: they must not change or stand in for the desktop save schema.

## Controls

The desktop controls, except F5, F9 and Esc (there is nothing to save or quit). On a touch screen, use the
toolbar: **Gather** / **Clear** (drag over tiles), **Hut** (tap a tile to plan a hut or cancel a plan), **Pause**,
speed **-** / **+**, **Fly** (the free-fly camera; **Look** then turns it by dragging, and an arrow pad moves it)
and **New colony**. `?seed=N` in the URL picks the seed; without it each visit gets a random one.

## Building

Needs emsdk (CI pins 6.0.11, as Strata does) and CMake 3.24 or newer:

```sh
emcmake cmake -S . -B build-web -DSTEADING_WEB=ON
cmake --build build-web -j8
ctest --test-dir build-web --output-on-failure   # the rules tests, as wasm under Node
cd build-web && python3 -m http.server 8000      # open http://localhost:8000/steading.html
```

`-DFETCHCONTENT_SOURCE_DIR_STRATA=<checkout>` builds against a local Strata checkout instead of the pinned
commit. The page is `steading.html` + `steading.js` + `steading.wasm` (about 1.2 MB); the shell is
`web/shell.html`.

## CI and deployment

`.github/workflows/browser.yml` builds the page, runs the rules tests under Node, and smoke-tests the page in
headless Chromium (SwiftShader) on a desktop viewport and the iPhone 13 profile with `web/smoke/smoke.cjs`. The
smoke test checks the fingerprint, that the canvas draws, a mouse drag, a touch tap and the buttons, then uploads
the screenshots. When the repository variable `ARCADE_ENABLED` is `true`, a push to `main` deploys the page to
arcade with `Stephenson-Software/arcade-deploy` and the `ARCADE_TOKEN` secret.
