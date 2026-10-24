// steading in a browser: the colony on Strata's WebGL 2 backend (Emscripten).
//
// The same rules (src/colony) and the same scene projection (app/scene.*) as
// the desktop game, with three differences:
//
//  - NO SAVES. The browser build has no OpenUSD, so it has no Strata save core
//    and writes nothing anywhere: a colony lives until the tab is closed or
//    reloaded. The desktop save format is not touched (this file does not
//    include or link anything from src/bridge or Strata's save module).
//  - Core content only: the Stats defaults and coreLooks() are built in, and
//    tests/test_core_defaults.cpp checks against content/prefabs.usda (the
//    desktop build's composition with no mods). Mods need the desktop game.
//  - The browser runs the frame loop (emscripten_set_main_loop_arg), and the
//    page (web/shell.html) adds buttons for touch screens through the
//    steading_* functions exported below.
//
// Keyboard and mouse are the desktop controls minus F5/F9/Esc: Space pause /
// resume (and keep playing after a win), +/- speed, left-drag designate
// (Ctrl+drag clears), right-click plan or cancel a hut, Tab free-fly camera
// (WASD move, Q/E down/up, arrows or middle-drag look, wheel dolly, Shift
// faster).
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "colony/generate.h"
#include "colony/looks.h"
#include "colony/stats.h"
#include "colony/systems.h"
#include "scene.h"
#include "strata/strata.hpp"

using namespace steading;

namespace {

constexpr int kSpace = 44, kEquals = 46, kMinus = 45, kKpPlus = 87, kKpMinus = 86, kTab = 43, kW = 26, kA = 4,
              kS = 22, kD = 7, kQ = 20, kE = 8, kRight = 79, kLeft = 80, kDown = 81, kUp = 82, kLShift = 225,
              kRShift = 229;
constexpr int kCtrl = 0x0040 | 0x0080;
constexpr int kMaxScancode = 512;
const int kSpeeds[] = {2, 5, 10, 20, 40};

// What a left-button drag (or a one-finger drag on a touch screen) does.
enum Tool { kGather = 0, kClear = 1, kHut = 2, kLook = 3 };

struct Game {
    std::unique_ptr<strata::Engine> engine;
    GenParams gen;
    Colony colony;
    Derived derived;
    const Stats stats{};  // core content (see the file comment)
    const Looks looks = coreLooks();
    std::vector<StrataRenderItem> items;
    int w = 1280, h = 800;        // drawable, pixels
    double cssW = 1280, cssH = 800;  // canvas, CSS pixels (mouse coordinates)
    app::View view;
    app::FlyView fly;
    bool flying = false;
    bool paused = false, dragging = false, looking = false, keepPlaying = false;
    int tool = kGather;
    int speed = 1, dragX = 0, dragY = 0;
    double acc = 0, last = 0;
    bool held[kMaxScancode] = {};
    std::string message;  // the last order's result, shown by the page
    std::string statusJson;
};

Game* g = nullptr;

float aspect() { return static_cast<float>(g->w) / static_cast<float>(std::max(1, g->h)); }

void say(const std::string& m) {
    g->message = m;
    std::printf("%s\n", m.c_str());
}

void startColony(uint64_t seed) {
    g->gen = GenParams{};
    g->gen.seed = seed;
    g->colony = generate(g->gen);
    g->derived = derive(g->colony);
    g->view = app::fitMap(g->colony, aspect());
    g->fly = app::flyFrom(g->view);
    g->paused = g->dragging = g->looking = g->keepPlaying = false;
    g->acc = 0;
    say("new colony, seed " + std::to_string(seed));
}

// Pixel (CSS) -> tile through whichever camera is showing; false off the ground.
bool pick(float px, float py, int& tx, int& ty) {
    const int cw = static_cast<int>(g->cssW), ch = static_cast<int>(g->cssH);
    if (g->flying) return app::pickTile(g->fly, aspect(), px, py, cw, ch, tx, ty);
    app::pickTile(g->view, aspect(), px, py, cw, ch, tx, ty);
    return true;
}

void togglePause() {
    if (outcome(g->colony) == Outcome::Won && !g->keepPlaying) {
        g->keepPlaying = true;
        g->paused = false;
        say("playing on past the win");
    } else {
        g->paused = !g->paused;
        say(g->paused ? "paused" : "playing");
    }
}

void toggleFly() {
    g->flying = !g->flying;
    if (g->flying) g->fly = app::flyFrom(g->view);
    g->dragging = g->looking = false;
    say(g->flying ? "free-fly camera" : "map view");
}

void hutAt(int tx, int ty) {
    if (!g->colony.grid.inBounds(tx, ty)) return;
    std::string why;
    if (cancelPlan(g->colony, g->derived, tx, ty)) say("hut plan cancelled");
    else if (planHut(g->colony, g->derived, tx, ty, &why)) say("hut planned");
    else say(why);
}

void handle(const StrataEvent& e) {
    if (e.type == STRATA_EVENT_RESIZED) {
        g->engine->drawableSize(g->w, g->h);
        g->view = app::fitMap(g->colony, aspect());
    } else if (e.type == STRATA_EVENT_KEY_UP) {
        if (e.scancode >= 0 && e.scancode < kMaxScancode) g->held[e.scancode] = false;
    } else if (e.type == STRATA_EVENT_KEY_DOWN && !e.repeat) {
        if (e.scancode >= 0 && e.scancode < kMaxScancode) g->held[e.scancode] = true;
        if (e.scancode == kSpace) togglePause();
        else if (e.scancode == kTab) toggleFly();
        else if (e.scancode == kEquals || e.scancode == kKpPlus) g->speed = std::min(g->speed + 1, 4);
        else if (e.scancode == kMinus || e.scancode == kKpMinus) g->speed = std::max(g->speed - 1, 0);
    } else if (e.type == STRATA_EVENT_MOUSE_BUTTON_DOWN) {
        int tx = -1, ty = -1;
        const bool onGround = pick(e.x, e.y, tx, ty);
        if (e.button == 2 || (e.button == 1 && g->tool == kLook)) g->looking = g->flying;
        else if (e.button == 1 && g->tool == kHut) { if (onGround) hutAt(tx, ty); }
        else if (e.button == 1 && onGround) { g->dragging = true; g->dragX = tx; g->dragY = ty; }
        else if (e.button == 3 && onGround) hutAt(tx, ty);
    } else if (e.type == STRATA_EVENT_MOUSE_BUTTON_UP && (e.button == 2 || e.button == 1) && g->looking) {
        g->looking = false;
    } else if (e.type == STRATA_EVENT_MOUSE_MOTION && g->looking) {
        app::flyTurn(g->fly, e.dx * 0.004f, -e.dy * 0.004f);
    } else if (e.type == STRATA_EVENT_MOUSE_WHEEL && g->flying) {
        app::flyMove(g->fly, e.y * std::max(2.0f, g->fly.eye[1] * 0.25f), 0, 0);
    } else if (e.type == STRATA_EVENT_MOUSE_BUTTON_UP && e.button == 1 && g->dragging) {
        int tx = g->dragX, ty = g->dragY;
        pick(e.x, e.y, tx, ty);  // released above the horizon: just the start tile
        const bool clear = g->tool == kClear || (e.modifiers & kCtrl) != 0;
        const int n = designate(g->colony, g->dragX, g->dragY, tx, ty, !clear);
        say(std::string(clear ? "cleared " : "designated ") + std::to_string(n) + " tile(s)");
        g->dragging = false;
    }
}

void simulate(double elapsed) {
    if (g->flying) {
        // Real-time, so the camera still moves while the colony is paused.
        const bool* k = g->held;
        const float boost = k[kLShift] || k[kRShift] ? 3.0f : 1.0f;
        const float dt = static_cast<float>(std::min(0.1, elapsed));
        const float move = dt * boost * (8.0f + g->fly.eye[1] * 0.8f);
        const float turn = dt * 1.5f;
        auto axis = [&](int pos, int neg) { return (k[pos] ? 1.0f : 0.0f) - (k[neg] ? 1.0f : 0.0f); };
        app::flyMove(g->fly, axis(kW, kS) * move, axis(kD, kA) * move, axis(kE, kQ) * move);
        app::flyTurn(g->fly, axis(kRight, kLeft) * turn, axis(kUp, kDown) * turn);
    }
    g->acc += elapsed;
    const double step = 1.0 / kSpeeds[g->speed];
    const Outcome state = outcome(g->colony);
    if (g->paused || state == Outcome::Lost || (state == Outcome::Won && !g->keepPlaying)) g->acc = 0;
    int guard = 0;
    while (g->acc >= step && guard++ < 8) {
        g->acc -= step;
        const Outcome before = outcome(g->colony);
        tick(g->colony, g->derived, g->stats);
        const Outcome now = outcome(g->colony);
        if (before == Outcome::Playing && now != Outcome::Playing && !g->keepPlaying) {
            if (now == Outcome::Won) {
                g->paused = true;
                say("the colony made it! Keep playing or start a new colony.");
            } else {
                say("the colony is gone.");
            }
            break;
        }
        if (g->keepPlaying && g->colony.settlers.size() == 0) {
            say("the colony is gone.");
            g->keepPlaying = false;
            break;
        }
    }
}

void draw() {
    g->engine->setCamera(g->flying ? app::flyCamera(g->fly, aspect()) : app::topDownCamera(g->view, aspect()));
    StrataFrameDesc fd{};
    fd.clear_flags = STRATA_CLEAR_COLOR | STRATA_CLEAR_DEPTH | STRATA_CLEAR_SKY;
    g->engine->beginFrame(fd);
    app::buildItems(g->colony, g->looks, g->stats, g->items);
    g->engine->submitPrimitives(g->items.data(), g->items.size());
    g->engine->endFrame();
    g->engine->present();
}

void frame(void*) {
    StrataEvent e;
    while (g->engine->pollEvent(e)) handle(e);
    const double now = emscripten_get_now() / 1000.0;
    const double elapsed = now - g->last;
    g->last = now;
    simulate(elapsed);
    draw();
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if (static_cast<unsigned char>(ch) < 0x20) o += ' ';
        else o += ch;
    }
    return o;
}

}  // namespace

// ---- Called by the page (web/shell.html) ------------------------------------
extern "C" {

EMSCRIPTEN_KEEPALIVE void steading_toggle_pause() { if (g) togglePause(); }
EMSCRIPTEN_KEEPALIVE void steading_toggle_fly() { if (g) toggleFly(); }
EMSCRIPTEN_KEEPALIVE void steading_speed(int delta) {
    if (g) g->speed = std::clamp(g->speed + delta, 0, 4);
}
EMSCRIPTEN_KEEPALIVE void steading_set_tool(int tool) {
    if (g) { g->tool = std::clamp(tool, 0, 3); g->dragging = g->looking = false; }
}
// Hold or release a key from an on-screen button (the fly camera's moves).
EMSCRIPTEN_KEEPALIVE void steading_hold(int scancode, int down) {
    if (g && scancode >= 0 && scancode < kMaxScancode) g->held[scancode] = down != 0;
}
EMSCRIPTEN_KEEPALIVE void steading_new_colony(double seed) {
    if (g) startColony(static_cast<uint64_t>(seed));
}
// The canvas changed size (CSS pixels).
EMSCRIPTEN_KEEPALIVE void steading_resize(double cssW, double cssH) {
    if (!g || cssW < 1 || cssH < 1) return;
    g->cssW = cssW;
    g->cssH = cssH;
    g->engine->resize(static_cast<int32_t>(cssW), static_cast<int32_t>(cssH));
    g->engine->drawableSize(g->w, g->h);
    g->view = app::fitMap(g->colony, aspect());
}
// The page's ?bench mode flies the free camera along a fixed path round the
// map: looking along `yawRad` (0 looks north, towards -Z), `pitchRad` below
// the horizon, from the map view's height times `distanceScale` away from the
// map's centre. Absolute, so the path is the same whatever the frame rate.
EMSCRIPTEN_KEEPALIVE void steading_bench_view(double yawRad, double pitchRad, double distanceScale) {
    if (!g || !(distanceScale > 0)) return;
    g->flying = true;
    g->dragging = g->looking = false;
    app::FlyView& f = g->fly;
    f.yaw = static_cast<float>(yawRad);
    f.pitch = -static_cast<float>(std::fabs(pitchRad));
    const float r = g->view.height * static_cast<float>(distanceScale);
    const float cp = std::cos(f.pitch);
    f.eye[0] = g->view.centreX - r * std::sin(f.yaw) * cp;  // r behind the centre, along the view
    f.eye[1] = std::max(0.5f, -r * std::sin(f.pitch));
    f.eye[2] = g->view.centreZ + r * std::cos(f.yaw) * cp;
}
// Run the colony headlessly for `ticks` ticks (no rendering); used by the
// smoke test to push the visible colony forward without waiting.
EMSCRIPTEN_KEEPALIVE void steading_advance(int ticks) {
    if (!g) return;
    for (int i = 0; i < ticks; ++i) tick(g->colony, g->derived, g->stats);
}
// The rules' fingerprint for a fresh colony after `ticks` ticks, as 16 hex
// digits. seed 42 and 1000 ticks must give tests/test_determinism.cpp's value.
EMSCRIPTEN_KEEPALIVE const char* steading_fingerprint(double seed, int ticks) {
    static char out[17];
    GenParams p;
    p.seed = static_cast<uint64_t>(seed);
    Colony c = generate(p);
    Derived d = derive(c);
    const Stats s{};
    for (int t = 0; t < ticks; ++t) tick(c, d, s);
    std::snprintf(out, sizeof out, "%016" PRIx64, fingerprint(c));
    return out;
}
// The HUD, as JSON.
EMSCRIPTEN_KEEPALIVE const char* steading_status() {
    if (!g) return "{}";
    const Colony& c = g->colony;
    const char* names[] = {"playing", "won", "lost"};
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"tick\":%" PRId64 ",\"season\":\"%s\",\"settlers\":%zu,\"cap\":%" PRId64 ",\"wood\":%" PRId64
                  ",\"stone\":%" PRId64 ",\"food\":%" PRId64 ",\"winters\":%" PRId64
                  ",\"outcome\":\"%s\",\"keepPlaying\":%s,\"paused\":%s,\"speed\":%d,\"tps\":%d,\"flying\":%s,"
                  "\"tool\":%d,\"seed\":%" PRIu64 ",\"message\":\"",
                  c.globals.tick, c.season == Season::Winter ? "winter" : "summer", c.settlers.size(),
                  housingCap(c, g->stats), c.globals.wood, c.globals.stone, c.globals.food, wintersSurvived(c),
                  names[static_cast<int>(outcome(c))], g->keepPlaying ? "true" : "false",
                  g->paused ? "true" : "false", g->speed, kSpeeds[g->speed], g->flying ? "true" : "false", g->tool,
                  g->gen.seed);
    g->statusJson = buf + jsonEscape(g->message) + "\"}";
    return g->statusJson.c_str();
}

}  // extern "C"

int main(int argc, char** argv) {
    static Game game;
    g = &game;
    uint64_t seed = 42;
    if (argc > 1) seed = std::strtoull(argv[1], nullptr, 10);

    double cw = 0, ch = 0;
    emscripten_get_element_css_size("#canvas", &cw, &ch);
    if (cw >= 1 && ch >= 1) { g->cssW = cw; g->cssH = ch; }

    strata::EngineDesc desc;
    desc.surface = STRATA_SURFACE_WINDOW;
    desc.width = static_cast<int32_t>(g->cssW);
    desc.height = static_cast<int32_t>(g->cssH);
    desc.title = "steading";
    try {
        g->engine = strata::Engine::create(desc);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "steading: cannot start the renderer: %s\n", ex.what());
        const std::string js = "if (Module.onRendererFailed) Module.onRendererFailed(\"" + jsonEscape(ex.what()) + "\")";
        emscripten_run_script(js.c_str());
        return 1;
    }
    g->engine->drawableSize(g->w, g->h);
    StrataLighting light = strata::lightingDefaults(STRATA_LIGHTING_SUN);
    light.time_of_day = 0.42f;
    light.fog = 0;
    g->engine->setLighting(light);

    startColony(seed);
    g->last = emscripten_get_now() / 1000.0;
    emscripten_run_script("if (Module.onGameReady) Module.onGameReady()");
    emscripten_set_main_loop_arg(frame, nullptr, 0, false);
    return 0;
}
