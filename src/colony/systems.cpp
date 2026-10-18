#include "systems.h"

#include <algorithm>
#include <cstdlib>

#include "rng.h"

namespace steading {

namespace {

constexpr int32_t kGrass = static_cast<int32_t>(Terrain::Grass);
constexpr int32_t kWater = static_cast<int32_t>(Terrain::Water);

bool walkable(const Grid& g, int32_t x, int32_t y) { return g.inBounds(x, y) && g.terrain[g.at(x, y)] != kWater; }

// One greedy step toward (tx, ty). Blocked by water: slide sideways. At the
// target already: random step. (No real pathfinding yet; settlers give up via
// patience and wander instead.)
void step(Colony& c, size_t i, int32_t tx, int32_t ty, Rng& rng) {
    const Grid& g = c.grid;
    int32_t& x = c.settlers.x[i];
    int32_t& y = c.settlers.y[i];
    const int32_t dx = (tx > x) - (tx < x), dy = (ty > y) - (ty < y);
    if (dx && walkable(g, x + dx, y)) {
        x += dx;
    } else if (dy && walkable(g, x, y + dy)) {
        y += dy;
    } else if (dx || dy) {
        const int32_t side = rng.below(2) ? 1 : -1;
        const int32_t sx = dx ? 0 : side, sy = dx ? side : 0;
        if (walkable(g, x + sx, y + sy)) {
            x += sx;
            y += sy;
        } else if (walkable(g, x - sx, y - sy)) {
            x -= sx;
            y -= sy;
        }
    } else {
        static const int32_t ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
        const int r = rng.below(4);
        if (walkable(g, x + ddx[r], y + ddy[r])) {
            x += ddx[r];
            y += ddy[r];
        }
    }
}

void wander(Colony& c, size_t i, Rng& rng) {
    const int32_t tx = c.settlers.x[i] + rng.range(-5, 5);
    const int32_t ty = c.settlers.y[i] + rng.range(-5, 5);
    step(c, i, tx, ty, rng);
}

// Step toward a target; false (and a wandering cooldown) after `limit` ticks.
bool seek(Colony& c, size_t i, int32_t tx, int32_t ty, int limit, Rng& rng) {
    step(c, i, tx, ty, rng);
    if (++c.settlers.patience[i] > limit) {
        c.settlers.patience[i] = -Rules::kWanderCooldown;
        return false;
    }
    return true;
}

// Ring search outward from (x, y) for a tile holding `want`. With
// designatedOnly, only player-designated tiles count.
bool findResource(const Grid& g, int32_t x, int32_t y, Resource want, int radius, bool designatedOnly,
                  int32_t& ox, int32_t& oy) {
    const int32_t w = static_cast<int32_t>(want);
    for (int rad = 1; rad <= radius; ++rad)
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; dx += (std::abs(dy) == rad ? 1 : 2 * rad)) {
                const int32_t px = x + dx, py = y + dy;
                if (!g.inBounds(px, py)) continue;
                const size_t k = g.at(px, py);
                if (g.resType[k] != w || g.resAmount[k] <= 0) continue;
                if (designatedOnly && !g.designated[k]) continue;
                ox = px;
                oy = py;
                return true;
            }
    return false;
}

Resource wanted(const Colony& c, const Stats& s, Rng& rng) {
    const Globals& G = c.globals;
    if (G.food < static_cast<int64_t>(c.settlers.size()) / 2) return Resource::Berry;
    if (G.wood < static_cast<int64_t>(s.hutWoodCost)) return Resource::Tree;
    return rng.below(3) ? Resource::Tree : Resource::Stone;
}

// A building claims its tile: any (depleted) resource there is cleared so it
// can never regrow under the building.
void claimTile(Colony& c, Derived& d, int32_t x, int32_t y) {
    const size_t k = c.grid.at(x, y);
    c.grid.resType[k] = 0;
    c.grid.resAmount[k] = 0;
    c.grid.designated[k] = 0;
    d.occupied[k] = 1;
}

bool buildable(const Colony& c, const Derived& d, int32_t x, int32_t y) {
    const Grid& g = c.grid;
    if (!g.inBounds(x, y)) return false;
    const size_t k = g.at(x, y);
    return g.terrain[k] == kGrass && !d.occupied[k] && !(g.resType[k] != 0 && g.resAmount[k] > 0);
}

}  // namespace

Derived derive(const Colony& c) {
    Derived d;
    d.occupied.assign(c.grid.cells(), 0);
    const Buildings& b = c.buildings;
    for (size_t r = 0; r < b.size(); ++r)
        if (c.grid.inBounds(b.x[r], b.y[r])) d.occupied[c.grid.at(b.x[r], b.y[r])] = 1;
    return d;
}

namespace systems {

void needs(Colony& c, std::vector<size_t>& dead) {
    Settlers& S = c.settlers;
    int64_t& food = c.globals.food;
    for (size_t i = 0; i < S.size(); ++i) {
        if (++S.hunger[i] >= Rules::kMealEvery && food > 0) {
            --food;
            S.hunger[i] = 0;
        }
        if (S.hunger[i] >= Rules::kStarveAt) dead.push_back(i);
    }
}

void tasks(Colony& c, const Stats& s, Rng& rng) {
    Settlers& S = c.settlers;
    Grid& g = c.grid;
    Globals& G = c.globals;
    const int32_t sx = static_cast<int32_t>(G.stockX), sy = static_cast<int32_t>(G.stockY);
    for (size_t i = 0; i < S.size(); ++i) {
        if (S.hunger[i] >= Rules::kStarveAt) continue;  // dying this tick
        switch (static_cast<Task>(S.task[i])) {
            case Task::Idle: {
                if (S.patience[i] < 0) {
                    ++S.patience[i];
                    wander(c, i, rng);
                    break;
                }
                const Resource want = wanted(c, s, rng);
                int32_t ox = 0, oy = 0;
                if (findResource(g, S.x[i], S.y[i], want, 2 * Rules::kSearchRadius, true, ox, oy) ||
                    findResource(g, S.x[i], S.y[i], want, Rules::kSearchRadius, false, ox, oy)) {
                    S.tx[i] = ox;
                    S.ty[i] = oy;
                    S.task[i] = static_cast<int32_t>(Task::ToResource);
                } else {
                    wander(c, i, rng);
                }
                break;
            }
            case Task::ToResource:
                if (S.x[i] == S.tx[i] && S.y[i] == S.ty[i]) {
                    S.task[i] = static_cast<int32_t>(Task::Gather);
                    S.patience[i] = 0;
                } else if (!seek(c, i, S.tx[i], S.ty[i], Rules::kGiveUpToResource, rng)) {
                    S.task[i] = static_cast<int32_t>(Task::Idle);
                }
                break;
            case Task::Gather: {
                const size_t k = g.at(S.tx[i], S.ty[i]);
                if (g.resAmount[k] > 0 && S.carry[i] < Rules::kCarryMax) {
                    --g.resAmount[k];
                    ++S.carry[i];
                    S.carryType[i] = g.resType[k];
                } else {
                    S.task[i] = static_cast<int32_t>(S.carry[i] > 0 ? Task::Return : Task::Idle);
                }
                break;
            }
            case Task::Return:
                if (S.x[i] == sx && S.y[i] == sy) {
                    switch (static_cast<Resource>(S.carryType[i])) {
                        case Resource::Tree: G.wood += S.carry[i]; break;
                        case Resource::Stone: G.stone += S.carry[i]; break;
                        case Resource::Berry: G.food += S.carry[i]; break;
                        case Resource::None: break;
                    }
                    S.carry[i] = 0;
                    S.carryType[i] = 0;
                    S.task[i] = static_cast<int32_t>(Task::Idle);
                    S.patience[i] = 0;
                } else if (S.patience[i] < 0) {
                    ++S.patience[i];
                    wander(c, i, rng);
                } else {
                    seek(c, i, sx, sy, Rules::kGiveUpReturning, rng);
                }
                break;
        }
    }
}

void removeDead(Colony& c, const std::vector<size_t>& dead) {
    for (auto it = dead.rbegin(); it != dead.rend(); ++it) c.settlers.remove(*it);
}

void build(Colony& c, Derived& d, const Stats& s) {
    Globals& G = c.globals;
    Buildings& B = c.buildings;
    const int64_t cost = static_cast<int64_t>(s.hutWoodCost);
    if (G.wood < cost) return;
    // A player plan (lowest id first) wins over automatic placement.
    for (size_t r = 0; r < B.size(); ++r)
        if (B.kind[r] == static_cast<int32_t>(BuildingKind::HutPlan)) {
            B.kind[r] = static_cast<int32_t>(BuildingKind::Hut);
            B.builtTick[r] = static_cast<int32_t>(G.tick);
            G.wood -= cost;
            return;
        }
    const int32_t sx = static_cast<int32_t>(G.stockX), sy = static_cast<int32_t>(G.stockY);
    for (int32_t rad = 2; rad <= 24; ++rad)
        for (int32_t dy = -rad; dy <= rad; dy += 2)
            for (int32_t dx = -rad; dx <= rad; dx += 2) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                const int32_t x = sx + dx, y = sy + dy;
                if (!buildable(c, d, x, y)) continue;
                B.add(G.nextBuildingId++, BuildingKind::Hut, x, y, static_cast<int32_t>(G.tick));
                claimTile(c, d, x, y);
                G.wood -= cost;
                return;
            }
}

void growth(Colony& c, const Stats& s) {
    Globals& G = c.globals;
    if (G.tick % Rules::kBirthEvery != 0 || G.food < Rules::kBirthFood) return;
    const int64_t cap = std::min(housingCap(c, s), G.maxSettlers);
    if (static_cast<int64_t>(c.settlers.size()) >= cap) return;
    c.settlers.add(G.nextSettlerId++, static_cast<int32_t>(G.stockX), static_cast<int32_t>(G.stockY));
    G.food -= Rules::kBirthFood;
}

void regrowth(Colony& c, const Stats& s, Rng& rng) {
    Grid& g = c.grid;
    const double perMille = c.season == Season::Winter ? s.regrowWinter : s.regrowSummer;
    const int cells = static_cast<int>(g.cells());
    const int tries = std::max(1, cells / 64);
    for (int k = 0; k < tries; ++k) {
        const size_t i = static_cast<size_t>(rng.below(cells));
        if (g.resType[i] == static_cast<int32_t>(Resource::Tree) && g.resAmount[i] == 0 &&
            rng.below(1000) < perMille)
            g.resAmount[i] = Rules::kTreeAmount;
    }
}

void seasons(Colony& c) {
    ++c.globals.tick;
    if (c.globals.tick % Rules::kSeasonLength == 0)
        c.season = c.season == Season::Summer ? Season::Winter : Season::Summer;
}

}  // namespace systems

void tick(Colony& c, Derived& d, const Stats& s) {
    Rng rng{static_cast<uint64_t>(c.globals.rng)};
    std::vector<size_t> dead;
    systems::needs(c, dead);
    systems::tasks(c, s, rng);
    systems::removeDead(c, dead);
    systems::build(c, d, s);
    systems::growth(c, s);
    systems::regrowth(c, s, rng);
    systems::seasons(c);
    c.globals.rng = static_cast<int64_t>(rng.state);
}

int designate(Colony& c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool on) {
    Grid& g = c.grid;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, g.w - 1);
    y1 = std::min(y1, g.h - 1);
    int changed = 0;
    for (int32_t y = y0; y <= y1; ++y)
        for (int32_t x = x0; x <= x1; ++x) {
            const size_t k = g.at(x, y);
            const int32_t want = on && g.resType[k] != 0 ? 1 : 0;
            if (g.designated[k] != want) {
                g.designated[k] = want;
                ++changed;
            }
        }
    return changed;
}

bool planHut(Colony& c, Derived& d, int32_t x, int32_t y, std::string* why) {
    if (!buildable(c, d, x, y)) {
        if (why) *why = "cannot build there (needs clear, unoccupied grass)";
        return false;
    }
    c.buildings.add(c.globals.nextBuildingId++, BuildingKind::HutPlan, x, y, static_cast<int32_t>(c.globals.tick));
    claimTile(c, d, x, y);
    return true;
}

bool cancelPlan(Colony& c, Derived& d, int32_t x, int32_t y) {
    Buildings& B = c.buildings;
    for (size_t r = 0; r < B.size(); ++r)
        if (B.kind[r] == static_cast<int32_t>(BuildingKind::HutPlan) && B.x[r] == x && B.y[r] == y) {
            B.remove(r);
            d.occupied[c.grid.at(x, y)] = 0;
            return true;
        }
    return false;
}

int64_t wintersSurvived(const Colony& c) { return c.globals.tick / (2 * Rules::kSeasonLength); }

int64_t housingCap(const Colony& c, const Stats& s) {
    int64_t huts = 0;
    for (int32_t k : c.buildings.kind) huts += k == static_cast<int32_t>(BuildingKind::Hut);
    return Rules::kBaseCapacity + static_cast<int64_t>(s.hutCapacity) * huts;
}

Outcome outcome(const Colony& c, const Goals& g) {
    if (c.settlers.size() == 0) return Outcome::Lost;
    if (static_cast<int64_t>(c.settlers.size()) >= g.population || wintersSurvived(c) >= g.winters)
        return Outcome::Won;
    return Outcome::Playing;
}

}  // namespace steading
