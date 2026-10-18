// Projects the colony into Strata render items (y-up world: tile (x, y) is the
// ground cell [x, x+1] x [y, y+1] in the XZ plane) and maps mouse positions back
// to tiles for the top-down camera.
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

void buildItems(const Colony& c, const Looks& looks, const Stats& stats, std::vector<StrataRenderItem>& out);
// Straight-down camera (screen up = -Z).
StrataCamera topDownCamera(const View& v, float aspect);
// Fit the whole map in view.
View fitMap(const Colony& c, float aspect);
// Pixel -> tile under the cursor (may be out of bounds).
void pickTile(const View& v, float aspect, float px, float py, int width, int height, int& tx, int& ty);

}  // namespace steading::app
