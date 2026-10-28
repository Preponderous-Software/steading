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
| Content | `content/prefabs.usda` + mods, composed through USD | core content only: the built-in `Stats` defaults (`src/colony/stats.h`) and `coreLooks()` (`src/colony/looks.h`). `tests/test_core_defaults.cpp` (desktop CI) checks that they equal the USD composition with no mods |
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

## The loading shell

The page loads behind the Strata web kit's loading shell
(`web/strata-kit/strata-loader.js`, vendored from Strata at the commit in
`web/strata-kit/PIN`; `web/strata-kit/update.sh <sha>` re-vendors it). It shows
the trailer's first frame (the one on <https://danielstephenson.dev/play>) as a blurred backdrop and real progress with the size ("0.5 of 1.3 MB"). The loader streams
the `.wasm` and hands its bytes to the engine, so nothing downloads twice. If
steading can't run (no WebGL 2, the renderer fails, the engine stops, a lost
WebGL context does not come back), the shell says why, and plays the trailer instead (the still under `prefers-reduced-motion`).
A device that draws under 12 frames a second is offered the clip without
losing the game (not in automated browsers or `?bench` runs).

`web/strata-boot.js` bridges this classic (non-`MODULARIZE`) build to the
loader: emcc's script tag sits inert in `<template id="strata-glue">`, the
bridge starts it, and the engine waits in `Module.instantiateWasm` for the
loader's bytes. After each link, `web/strata_page.cmake` puts the loader and
the bridge inline in the page (so the site is still the same four files) and stamps the download's size on the
template, because the arcade compresses on the fly and sends no
`Content-Length`.

## Measuring the frame rate

Open the page with `?bench` (<https://steading.play.danielstephenson.dev/?bench>) on the device to measure. It
starts colony seed 42 (or `?seed=N`), and the free camera turns once around the map, dipping lower and closer
halfway, for 15 seconds after a short warm-up while the colony runs. Then it shows the frame rate, the frame time
(median and p95), the page's own work per frame, the GPU time where the browser offers WebGL 2 timer queries, the
canvas size and device pixel ratio and the GPU's name, with a button that copies it all as text. `?bench=30` runs
for 30 seconds. Nothing is sent anywhere. `web/bench.js` is the harness and `steading_bench_view` puts the camera on
the path (absolute in time, so runs on different devices are comparable).

## CI and deployment

`.github/workflows/browser.yml` builds the page, runs the rules tests under Node, and smoke-tests the page in
headless Chromium (SwiftShader) on a desktop viewport and the iPhone 13 profile with `web/smoke/smoke.cjs`. The
smoke test checks the fingerprint, that the canvas draws, a mouse drag, a touch tap and the buttons, then uploads
the screenshots. `web/smoke/bench.cjs` checks that a short `?bench` run finishes and shows its card on both
profiles (it keeps no numbers: SwiftShader draws on the CPU). When the repository variable `ARCADE_ENABLED` is `true`, a push to `main` deploys the page to
arcade with `Stephenson-Software/arcade-deploy` and the `ARCADE_TOKEN` secret.
