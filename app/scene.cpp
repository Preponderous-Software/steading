#include "scene.h"

#include <algorithm>
#include <cmath>

#include "strata/strata.hpp"

namespace steading::app {

namespace {

constexpr float kPi = 3.14159265358979f;

void push(std::vector<StrataRenderItem>& out, int kind, float x, float y, float z, float sx, float sy, float sz,
          const float rgb[3]) {
    StrataRenderItem it{};
    it.kind = kind;
    it.position[0] = x;
    it.position[1] = y;
    it.position[2] = z;
    it.scale[0] = sx;
    it.scale[1] = sy;
    it.scale[2] = sz;
    it.color[0] = rgb[0];
    it.color[1] = rgb[1];
    it.color[2] = rgb[2];
    it.color[3] = 1;
    out.push_back(it);
}

float cell(int32_t v) { return static_cast<float>(v) + 0.5f; }

}  // namespace

void buildItems(const Colony& c, const Looks& looks, const Stats& stats, std::vector<StrataRenderItem>& out) {
    out.clear();
    const Grid& g = c.grid;
    const bool winter = c.season == Season::Winter;
    static const float grass[3] = {0.35f, 0.62f, 0.30f}, grassWinter[3] = {0.78f, 0.82f, 0.84f},
                       water[3] = {0.15f, 0.35f, 0.75f}, rock[3] = {0.60f, 0.58f, 0.55f},
                       stone[3] = {0.42f, 0.42f, 0.46f}, berry[3] = {0.62f, 0.25f, 0.55f},
                       marked[3] = {0.95f, 0.55f, 0.15f};
    for (int32_t y = 0; y < g.h; ++y)
        for (int32_t x = 0; x < g.w; ++x) {
            const size_t k = g.at(x, y);
            const auto t = static_cast<Terrain>(g.terrain[k]);
            const float* col = t == Terrain::Water ? water : t == Terrain::Rock ? rock : (winter ? grassWinter : grass);
            const float h = t == Terrain::Water ? 0.1f : t == Terrain::Rock ? 0.4f : 0.2f;
            push(out, STRATA_PRIMITIVE_CUBE, cell(x), h / 2 - 0.2f, cell(y), 1, h, 1, col);
            const int amount = g.resAmount[k];
            const auto r = static_cast<Resource>(g.resType[k]);
            if (g.designated[k]) push(out, STRATA_PRIMITIVE_CUBE, cell(x), 0.01f, cell(y), 0.95f, 0.02f, 0.95f, marked);
            if (amount <= 0) continue;
            if (r == Resource::Tree) {
                const float s = 0.3f + 0.05f * static_cast<float>(amount);
                push(out, STRATA_PRIMITIVE_CYLINDER, cell(x), s, cell(y), 0.45f, s, 0.45f,
                     winter ? looks.treeWinter : looks.treeSummer);
            } else if (r == Resource::Stone) {
                push(out, STRATA_PRIMITIVE_CUBE, cell(x), 0.3f, cell(y), 0.5f, 0.3f, 0.5f, stone);
            } else if (r == Resource::Berry) {
                push(out, STRATA_PRIMITIVE_SPHERE, cell(x), 0.25f, cell(y), 0.45f, 0.45f, 0.45f, berry);
            }
        }
    const Buildings& B = c.buildings;
    for (size_t i = 0; i < B.size(); ++i) {
        const auto kind = static_cast<BuildingKind>(B.kind[i]);
        if (kind == BuildingKind::Stockpile)
            push(out, STRATA_PRIMITIVE_CUBE, cell(B.x[i]), 0.15f, cell(B.y[i]), 1, 0.3f, 1, looks.stockpile);
        else if (kind == BuildingKind::Hut)
            push(out, STRATA_PRIMITIVE_CUBE, cell(B.x[i]), 0.45f, cell(B.y[i]), 0.85f, 0.9f, 0.85f, looks.hut);
        else
            push(out, STRATA_PRIMITIVE_CUBE, cell(B.x[i]), 0.05f, cell(B.y[i]), 0.85f, 0.1f, 0.85f, looks.hutPlan);
    }
    const Settlers& S = c.settlers;
    for (size_t i = 0; i < S.size(); ++i) {
        push(out, STRATA_PRIMITIVE_SPHERE, cell(S.x[i]), 0.35f, cell(S.y[i]), 0.55f, 0.55f, 0.55f, looks.settler);
        if (S.carry[i] > 0) {
            static const float load[3] = {0.4f, 0.25f, 0.1f};
            push(out, STRATA_PRIMITIVE_CUBE, cell(S.x[i]), 0.75f, cell(S.y[i]), 0.25f, 0.25f, 0.25f,
                 S.carryType[i] == static_cast<int32_t>(Resource::Berry) ? berry : load);
        }
    }
    // HUD bars along the top edge of the map: wood, food, settlers vs housing, winters.
    const Globals& G = c.globals;
    struct Bar {
        double value, max;
        const float* col;
    };
    static const float woodCol[3] = {0.55f, 0.35f, 0.2f}, white[3] = {0.95f, 0.95f, 0.95f};
    const double cap = static_cast<double>(housingCap(c, stats));
    const Bar bars[] = {{static_cast<double>(G.wood), 100, woodCol},
                        {static_cast<double>(G.food), 100, berry},
                        {static_cast<double>(S.size()), std::max(1.0, cap), looks.settler},
                        {static_cast<double>(wintersSurvived(c)), 5, white}};
    for (int b = 0; b < 4; ++b) {
        const float len = static_cast<float>(std::min(1.0, bars[b].value / bars[b].max)) * static_cast<float>(g.w);
        if (len > 0)
            push(out, STRATA_PRIMITIVE_CUBE, len / 2, 0.1f, -1.0f - 1.2f * static_cast<float>(b), len, 0.2f, 0.8f,
                 bars[b].col);
    }
}

StrataCamera topDownCamera(const View& v, float aspect) {
    const float eye[3] = {v.centreX, v.height, v.centreZ};
    const float target[3] = {v.centreX, 0, v.centreZ};
    const float up[3] = {0, 0, -1};
    return strata::cameraLookAt(eye, target, up, v.fovY, aspect, 1.0f, v.height + 50);
}

View fitMap(const Colony& c, float aspect) {
    View v;
    v.centreX = static_cast<float>(c.grid.w) / 2;
    v.centreZ = static_cast<float>(c.grid.h) / 2 - 2.5f;  // leave room for the HUD bars
    const float halfH = static_cast<float>(c.grid.h) / 2 + 6;
    const float halfW = static_cast<float>(c.grid.w) / 2 + 2;
    const float need = std::max(halfH, halfW / aspect);
    v.height = need / std::tan(v.fovY * kPi / 360.f);
    return v;
}

void pickTile(const View& v, float aspect, float px, float py, int width, int height, int& tx, int& ty) {
    const float nx = 2 * px / static_cast<float>(width) - 1;
    const float ny = 1 - 2 * py / static_cast<float>(height);
    const float halfH = v.height * std::tan(v.fovY * kPi / 360.f);
    const float wx = v.centreX + nx * halfH * aspect;
    const float wz = v.centreZ - ny * halfH;
    tx = static_cast<int>(std::floor(wx));
    ty = static_cast<int>(std::floor(wz));
}

namespace {

void flyAxes(const FlyView& f, float fwd[3], float right[3], float up[3]) {
    const float cp = std::cos(f.pitch), sp = std::sin(f.pitch), cy = std::cos(f.yaw), sy = std::sin(f.yaw);
    fwd[0] = sy * cp;
    fwd[1] = sp;
    fwd[2] = -cy * cp;
    right[0] = cy;  // fwd x world-up, normalised
    right[1] = 0;
    right[2] = sy;
    up[0] = right[1] * fwd[2] - right[2] * fwd[1];  // right x fwd
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
}

}  // namespace

StrataCamera flyCamera(const FlyView& f, float aspect) {
    float fwd[3], right[3], up[3];
    flyAxes(f, fwd, right, up);
    const float target[3] = {f.eye[0] + fwd[0], f.eye[1] + fwd[1], f.eye[2] + fwd[2]};
    return strata::cameraLookAt(f.eye, target, up, f.fovY, aspect, 0.1f, 1000.0f);
}

FlyView flyFrom(const View& v) {
    FlyView f;
    const float back = v.height * 0.6f;
    f.pitch = -0.7f;
    f.eye[0] = v.centreX;
    f.eye[1] = back * std::tan(-f.pitch);
    f.eye[2] = v.centreZ + back;
    return f;
}

void flyMove(FlyView& f, float fwd, float side, float up) {
    const float cy = std::cos(f.yaw), sy = std::sin(f.yaw);
    f.eye[0] += sy * fwd + cy * side;
    f.eye[1] = std::max(0.5f, f.eye[1] + up);
    f.eye[2] += -cy * fwd + sy * side;
}

void flyTurn(FlyView& f, float dyaw, float dpitch) {
    f.yaw = std::remainder(f.yaw + dyaw, 2 * kPi);
    f.pitch = std::clamp(f.pitch + dpitch, -1.55f, 1.55f);
}

bool pickTile(const FlyView& f, float aspect, float px, float py, int width, int height, int& tx, int& ty) {
    float fwd[3], right[3], up[3];
    flyAxes(f, fwd, right, up);
    const float nx = 2 * px / static_cast<float>(width) - 1;
    const float ny = 1 - 2 * py / static_cast<float>(height);
    const float t = std::tan(f.fovY * kPi / 360.f);
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = fwd[i] + right[i] * nx * t * aspect + up[i] * ny * t;
    if (d[1] >= -1e-4f) return false;
    const float s = -f.eye[1] / d[1];  // where the ray meets the ground plane y = 0
    tx = static_cast<int>(std::floor(f.eye[0] + d[0] * s));
    ty = static_cast<int>(std::floor(f.eye[2] + d[2] * s));
    return true;
}

}  // namespace steading::app
