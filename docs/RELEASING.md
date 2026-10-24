# Releasing

A release is a bare version tag on `main` (`0.1.0`, never `v0.1.0`) with a GitHub release on it.
Publishing the release starts [`.github/workflows/release.yml`](../.github/workflows/release.yml),
which builds the desktop game for Linux x64 and Windows x64 and attaches the downloads to the
release. macOS is not built (Strata has no macOS build). The browser build is separate: it deploys
on every push to `main` ([WEB.md](WEB.md)) and has no saves.

## Steps

1. **Set the version.** On a branch, set the version in `project(steading VERSION ...)` at the top
   of [`CMakeLists.txt`](../CMakeLists.txt) and open a pull request. The release workflow refuses a
   tag that does not match it.

2. **Create the release on the merged commit.** Pass the full SHA, not a branch name, so the tag
   lands on exactly the commit that was checked:

   ```bash
   git fetch origin
   sha=$(git rev-parse origin/main)
   gh release create 0.1.0 --target "$sha" --prerelease \
     --title "steading 0.1.0" --notes-file notes.md
   ```

3. **Wait for the builds.** The `Release` workflow runs on the published release and adds four
   assets:

   | Asset | Contents |
   | --- | --- |
   | `Steading-<version>-linux-x64.tar.gz` | `steading.sh`, `bin/steading`, `content/`, `mods/`, OpenUSD and oneTBB in `lib/` (and `plugin/`), `licenses/`, `README.txt`, `LICENSE.txt`, `THIRD-PARTY.md` |
   | `Steading-<version>-win-x64.zip` | `steading.bat`, `steading.exe`, the Visual C++ runtime DLLs, `content/`, `mods/`, OpenUSD and oneTBB in `lib/` and `bin/` (and `plugin/`), and the same documents |
   | `*.sha256` | The SHA-256 of each archive |

   If the workflow fails, fix the cause and re-run it (`gh run rerun <id>`); uploads replace assets
   of the same name.

Pushing a version tag without creating a release also works: the workflow builds the tag and
creates a pre-release for it with generated notes.

## Saves and releases

`tests/fixtures/` holds save slots written by the game itself, one or more per save format version
(`kSchemaVersion` in `src/colony/schema.h`), and `tests/test_save_fixtures.cpp` loads every one of
them. A released fixture is never edited or regenerated. A change to the save format bumps
`kSchemaVersion`, adds a migration, and adds a `v<N>-` fixture written by the game at the new
version, in the same pull request ([`tests/fixtures/README.md`](../tests/fixtures/README.md)).

## What is in a download

Strata and SDL3 are linked into the executable. OpenUSD is not: the game loads Strata's prebuilt
OpenUSD (the release asset `ci.yml` uses, keyed by Strata's `modules/usd/openusd` lock) at run
time, so the download carries its shared libraries and plugins, and the launcher (`steading.sh`,
`steading.bat`) puts them on the library path and passes `--game-dir` so the game finds its
`content/` and `mods/`. OpenUSD is built against the dynamic C++ runtime, so the Windows download
also carries the Visual C++ runtime DLLs. The player needs an OpenGL 4.5 graphics driver on
Windows 10/11 x64 or Linux x64, and on Linux glibc 2.39 or newer: the Linux build runs in the same
`ubuntu:24.04` container as `ci.yml`, because the OpenUSD prebuilt is built there.

What each run checks:

- **Linux:** with the OpenUSD install it was built against moved away, the packaged game (through
  `steading.sh`) starts a new colony headless on Mesa llvmpipe, saves it, and continues from that
  save to tick 500.
- **Windows:** the runner cannot draw (no headless surface on Windows). `steading.bat` must start
  `steading.exe` with only the download's DLLs on `PATH`: it is given an unknown argument and must
  print its usage and exit 2.

## Checking the packaging without a release

A pull request that changes `release.yml` or `CMakeLists.txt` runs the same builds as a dry run,
keeping the archives as workflow artifacts. So does running it by hand, once the workflow is on
`main`:

```bash
gh workflow run release.yml --ref <branch>             # dry run
gh workflow run release.yml --ref main -f tag=0.1.0    # build a tag and attach to its release
```
