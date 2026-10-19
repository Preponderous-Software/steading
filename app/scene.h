// Projects the colony into Strata render items (y-up world: tile (x, y) is the
// ground cell [x, x+1] x [y, y+1] in the XZ plane) and maps mouse positions back
// to tiles for the top-down camera and the free-fly camera.
#pragma once

#include <vector>

#include "bridge/session.h"
#include "colony/systems.h"
#include "strata/strata.h"

namespace steading::app {

struct View {
    float centreX = 32, centreZ = 32;  // what the camera looks at
    float height = 80;                 // eye height above the ground
    float fovY = 45;                   // degrees
};

// Free-fly camera: an eye that can go anywhere above the ground. yaw 0 looks
// towards -Z (screen up in the top-down view), positive yaw turns right;
// pitch is in radians, negative looks down.
struct FlyView {
    float eye[3] = {32, 30, 80};
    float yaw = 0, pitch = -0.6f;
    float fovY = 60;  // degrees
};

void buildItems(const Colony& c, const Looks& looks, const Stats& stats, std::vector<StrataRenderItem>& out);
// Straight-down camera (screen up = -Z).
StrataCamera topDownCamera(const View& v, float aspect);
// Fit the whole map in view.
View fitMap(const Colony& c, float aspect);
// Pixel -> tile under the cursor (may be out of bounds).
void pickTile(const View& v, float aspect, float px, float py, int width, int height, int& tx, int& ty);

StrataCamera flyCamera(const FlyView& f, float aspect);
// A fly view that starts out looking at what the top-down view was showing,
// from the south and above, at an angle.
FlyView flyFrom(const View& v);
// Move by `fwd`/`side`/`up` units along the view's ground-level forward and
// right directions and world up; stays above the ground.
void flyMove(FlyView& f, float fwd, float side, float up);
// Turn by `dyaw`/`dpitch` radians; pitch stays short of straight up/down.
void flyTurn(FlyView& f, float dyaw, float dpitch);
// Pixel -> tile on the ground under the cursor. False when the cursor is
// above the horizon (tx/ty are then left unchanged).
bool pickTile(const FlyView& f, float aspect, float px, float py, int width, int height, int& tx, int& ty);

}  // namespace steading::app
