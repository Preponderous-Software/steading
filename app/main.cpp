// steading: the playable colony on the Strata engine.
//
//   steading [--slot DIR] [--new SEED] [--size N] [--settlers N] [--mods a,b]
//            [--force-mods] [--headless --ticks N --screenshot out.png]
//            [--frames DIR --frame-every N --width W --height H]   (headless timelapse frames)
//
// Controls: Space pause/resume, +/- speed, left-drag designate tiles for
// gathering (Ctrl+drag clears), right-click plan or cancel a hut, F5 save,
// F9 reload the last save, H help, Esc quit. The game also autosaves every
// 500 ticks, off the main thread.
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <sstream>
#include <string>
#include <vector>

#include "bridge/session.h"
#include "scene.h"
#include "strata/strata.hpp"

#ifndef STEADING_GAME_DIR
#define STEADING_GAME_DIR "."
#endif

using namespace steading;

namespace {

constexpr int kSpace = 44, kEscape = 41, kEquals = 46, kMinus = 45, kKpPlus = 87, kKpMinus = 86, kF5 = 62,
              kF9 = 66, kH = 11;
constexpr int kCtrl = 0x0040 | 0x0080;
const int kSpeeds[] = {2, 5, 10, 20, 40};

struct Args {
    std::string slot = "saves/default";
    std::string gameDir = STEADING_GAME_DIR;
    bool fresh = false;
    GenParams gen;
    std::vector<std::string> mods;
    bool forceMods = false;
    bool headless = false;
    long ticks = 0;
    std::string screenshot;
    std::string framesDir;  // headless: write a PNG every frameEvery ticks
    long frameEvery = 10;
    int width = 1280, height = 800;
    float zoom = 1;  // >1 zooms in on the stockpile
};

void help() {
    std::puts("steading controls:\n"
              "  Space        pause / resume        +/-  speed\n"
              "  Left-drag    designate tiles for gathering (Ctrl: clear)\n"
              "  Right-click  plan a hut (or cancel a plan)\n"
              "  F5           save now              F9   reload the last save\n"
              "  H            this help             Esc  quit (saves first)");
}

void status(const Session& s, const char* note = "") {
    const Colony& c = const_cast<Session&>(s).colony();
    const char* names[] = {"playing", "WON", "LOST"};
    std::printf("tick %" PRId64 " %s | settlers %zu/%" PRId64 " | wood %" PRId64 " stone %" PRId64 " food %" PRId64
                " | winters %" PRId64 " | %s %s\n",
                c.globals.tick, c.season == Season::Winter ? "winter" : "summer", c.settlers.size(),
                housingCap(c, s.stats()), c.globals.wood, c.globals.stone, c.globals.food, wintersSurvived(c),
                names[static_cast<int>(outcome(c))], note);
    std::fflush(stdout);
}

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (k == "--slot") a.slot = val();
        else if (k == "--game-dir") a.gameDir = val();
        else if (k == "--new") { a.fresh = true; a.gen.seed = std::strtoull(val().c_str(), nullptr, 10); }
        else if (k == "--size") a.gen.width = a.gen.height = std::atoi(val().c_str());
        else if (k == "--settlers") a.gen.settlers = std::atoi(val().c_str());
        else if (k == "--mods") {
            std::stringstream ss(val());
            std::string id;
            while (std::getline(ss, id, ',')) if (!id.empty()) a.mods.push_back(id);
        }
        else if (k == "--force-mods") a.forceMods = true;
        else if (k == "--headless") a.headless = true;
        else if (k == "--ticks") a.ticks = std::atol(val().c_str());
        else if (k == "--screenshot") a.screenshot = val();
        else if (k == "--frames") a.framesDir = val();
        else if (k == "--frame-every") a.frameEvery = std::max(1L, std::atol(val().c_str()));
        else if (k == "--zoom") a.zoom = std::max(1.0f, static_cast<float>(std::atof(val().c_str())));
        else if (k == "--width") a.width = std::atoi(val().c_str());
        else if (k == "--height") a.height = std::atoi(val().c_str());
        else {
            std::fprintf(stderr, "unknown argument %s\n", k.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;
    std::string err;
    auto session = Session::open({a.gameDir, a.slot, a.mods}, &err);
    if (!session) {
        std::fprintf(stderr, "steading: %s\n", err.c_str());
        return 1;
    }
    if (a.fresh || !session->hasGame()) {
        if (!session->newGame(a.gen, &err)) {
            std::fprintf(stderr, "steading: new game: %s\n", err.c_str());
            return 1;
        }
        std::printf("new colony, seed %" PRIu64 ", saved to %s\n", a.gen.seed, a.slot.c_str());
    } else {
        strata::save::LoadOptions lo;
        lo.allowModMismatch = a.forceMods;
        auto r = session->load(lo);
        for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
        for (const auto& x : r.rejected) std::printf("rejected: %s\n", x.c_str());
        if (!r.ok) {
            std::fprintf(stderr, "steading: cannot load %s: %s\n", a.slot.c_str(), r.error.c_str());
            if (r.error.find("mod list differs") != std::string::npos)
                std::fprintf(stderr, "  (pass --force-mods to continue with the current mods; type-level stats change)\n");
            return 1;
        }
        std::printf("loaded %s\n", r.saveFile.empty() ? "a new colony" : r.saveFile.c_str());
    }

    strata::EngineDesc desc;
    desc.surface = a.headless ? STRATA_SURFACE_HEADLESS : STRATA_SURFACE_WINDOW;
    desc.width = a.width;
    desc.height = a.height;
    desc.title = "steading";
    desc.flags = a.headless ? 0 : STRATA_FLAG_VSYNC;
    std::unique_ptr<strata::Engine> engine;
    try {
        engine = strata::Engine::create(desc);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "steading: cannot start the renderer: %s\n", e.what());
        return 1;
    }
    StrataLighting light = strata::lightingDefaults(STRATA_LIGHTING_SUN);
    light.time_of_day = 0.42f;
    light.fog = 0;
    engine->setLighting(light);

    std::vector<StrataRenderItem> items;
    int w = desc.width, h = desc.height;
    auto aspect = [&] { return static_cast<float>(w) / static_cast<float>(std::max(1, h)); };
    app::View view = app::fitMap(session->colony(), aspect());
    if (a.zoom > 1) {
        view.height /= a.zoom;
        view.centreX = static_cast<float>(session->colony().globals.stockX) + 0.5f;
        view.centreZ = static_cast<float>(session->colony().globals.stockY) + 0.5f;
    }

    auto draw = [&] {
        engine->setCamera(app::topDownCamera(view, aspect()));
        StrataFrameDesc fd{};
        fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY;
        engine->beginFrame(fd);
        app::buildItems(session->colony(), session->looks(), session->stats(), items);
        engine->submitPrimitives(items.data(), items.size());
        engine->endFrame();
    };

    if (a.headless) {
        int frame = 0;
        auto shoot = [&] {
            char name[64];
            std::snprintf(name, sizeof name, "/frame-%05d.png", frame++);
            draw();
            engine->saveScreenshotPng(a.framesDir + name);
        };
        for (long t = 0; t < a.ticks; ++t) {
            if (!a.framesDir.empty() && t % a.frameEvery == 0) shoot();
            session->tick();
        }
        if (!a.framesDir.empty()) shoot();
        draw();
        if (!a.screenshot.empty()) engine->saveScreenshotPng(a.screenshot);
        auto r = session->save();
        status(*session, r.ok ? "(saved)" : r.error.c_str());
        return r.ok ? 0 : 1;
    }

    help();
    status(*session);
    using Clock = std::chrono::steady_clock;
    bool paused = false, running = true, dragging = false;
    int speed = 1, dragX = 0, dragY = 0;
    double acc = 0;
    auto last = Clock::now();
    std::future<strata::save::SaveResult> pending;
    auto startSave = [&](const char* why) {
        if (pending.valid()) return;  // one at a time
        pending = session->saveAsync();
        std::printf("saving (%s)...\n", why);
    };
    auto pollSave = [&] {
        if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto r = pending.get();
            if (r.ok) std::printf("saved %s (%" PRIu64 " bytes; main thread %.2f ms)\n", r.file.c_str(), r.bytes, r.snapshotMs);
            else std::printf("SAVE FAILED: %s (the previous save is intact)\n", r.error.c_str());
        }
    };
    while (running) {
        StrataEvent e;
        while (engine->pollEvent(e)) {
            if (e.type == STRATA_EVENT_QUIT) running = false;
            else if (e.type == STRATA_EVENT_RESIZED) {
                engine->drawableSize(w, h);
                view = app::fitMap(session->colony(), aspect());
            } else if (e.type == STRATA_EVENT_KEY_DOWN && !e.repeat) {
                if (e.scancode == kEscape) running = false;
                else if (e.scancode == kSpace) { paused = !paused; status(*session, paused ? "(paused)" : ""); }
                else if (e.scancode == kEquals || e.scancode == kKpPlus) speed = std::min(speed + 1, 4);
                else if (e.scancode == kMinus || e.scancode == kKpMinus) speed = std::max(speed - 1, 0);
                else if (e.scancode == kF5) startSave("F5");
                else if (e.scancode == kF9) {
                    if (pending.valid()) pending.wait();
                    pollSave();
                    auto r = session->load();
                    for (const auto& x : r.warnings) std::printf("warning: %s\n", x.c_str());
                    std::printf("%s\n", r.ok ? "reloaded the last save" : ("reload failed: " + r.error).c_str());
                }
                else if (e.scancode == kH) help();
            } else if (e.type == STRATA_EVENT_MOUSE_BUTTON_DOWN) {
                int tx, ty;
                app::pickTile(view, aspect(), e.x, e.y, w, h, tx, ty);
                if (e.button == 1) { dragging = true; dragX = tx; dragY = ty; }
                else if (e.button == 3 && session->colony().grid.inBounds(tx, ty)) {
                    std::string why;
                    if (!cancelPlan(session->colony(), session->derived(), tx, ty) &&
                        !planHut(session->colony(), session->derived(), tx, ty, &why))
                        std::printf("%s\n", why.c_str());
                }
            } else if (e.type == STRATA_EVENT_MOUSE_BUTTON_UP && e.button == 1 && dragging) {
                int tx, ty;
                app::pickTile(view, aspect(), e.x, e.y, w, h, tx, ty);
                const bool clear = (e.modifiers & kCtrl) != 0;
                const int n = designate(session->colony(), dragX, dragY, tx, ty, !clear);
                std::printf("%s %d tile(s)\n", clear ? "cleared" : "designated", n);
                dragging = false;
            }
        }
        const auto now = Clock::now();
        acc += std::chrono::duration<double>(now - last).count();
        last = now;
        const double step = 1.0 / kSpeeds[speed];
        if (paused || outcome(session->colony()) != Outcome::Playing) acc = 0;
        int guard = 0;
        while (acc >= step && guard++ < 8) {
            acc -= step;
            const Outcome before = outcome(session->colony());
            session->tick();
            const Colony& c = session->colony();
            if (c.globals.tick % 100 == 0) status(*session);
            if (c.globals.tick % 500 == 0) startSave("autosave");
            if (before == Outcome::Playing && outcome(c) != Outcome::Playing) {
                status(*session, outcome(c) == Outcome::Won ? "-- the colony made it!" : "-- the colony is gone.");
                startSave("end of game");
            }
        }
        pollSave();
        draw();
        engine->present();
    }
    if (pending.valid()) pending.wait();
    pollSave();
    auto r = session->save();
    std::printf("%s\n", r.ok ? "saved on exit" : ("SAVE ON EXIT FAILED: " + r.error).c_str());
    return r.ok ? 0 : 1;
}
