#include "components.h"

#include <sstream>
#include <stdexcept>

namespace steading {

namespace {

template <typename T>
void eraseAt(std::vector<T>& v, size_t row) {
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(row));
}

template <typename T>
bool firstDiff(const char* name, const std::vector<T>& a, const std::vector<T>& b, std::string* why) {
    if (a == b) return false;
    if (why) {
        std::ostringstream o;
        if (a.size() != b.size()) {
            o << name << ": size " << a.size() << " vs " << b.size();
        } else {
            for (size_t i = 0; i < a.size(); ++i)
                if (!(a[i] == b[i])) {
                    o << name << "[" << i << "]: " << a[i] << " vs " << b[i];
                    break;
                }
        }
        *why = o.str();
    }
    return true;
}

struct Fnv {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ULL;
        }
    }
    template <typename T>
    void vec(const std::vector<T>& v) {
        const uint64_t n = v.size();
        bytes(&n, sizeof n);
        if (!v.empty()) bytes(v.data(), v.size() * sizeof(T));
    }
    void i64(int64_t v) { bytes(&v, sizeof v); }
};

}  // namespace

size_t Settlers::add(int64_t newId, int32_t px, int32_t py) {
    if (!id.empty() && newId <= id.back()) throw std::logic_error("settler ids must increase");
    id.push_back(newId);
    x.push_back(px);
    y.push_back(py);
    for (auto* col : {&task, &tx, &ty, &carry, &carryType, &hunger, &patience}) col->push_back(0);
    return id.size() - 1;
}

void Settlers::remove(size_t row) {
    eraseAt(id, row);
    for (auto* col : {&x, &y, &task, &tx, &ty, &carry, &carryType, &hunger, &patience}) eraseAt(*col, row);
}

size_t Buildings::add(int64_t newId, BuildingKind k, int32_t px, int32_t py, int32_t tick) {
    if (!id.empty() && newId <= id.back()) throw std::logic_error("building ids must increase");
    id.push_back(newId);
    kind.push_back(static_cast<int32_t>(k));
    x.push_back(px);
    y.push_back(py);
    builtTick.push_back(tick);
    return id.size() - 1;
}

void Buildings::remove(size_t row) {
    eraseAt(id, row);
    for (auto* col : {&kind, &x, &y, &builtTick}) eraseAt(*col, row);
}

void Grid::init(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("grid size must be positive");
    w = width;
    h = height;
    for (auto* ch : {&terrain, &resType, &resAmount, &designated}) ch->assign(cells(), 0);
}

bool equal(const Colony& a, const Colony& b, std::string* why) {
    auto fail = [&](const std::string& s) {
        if (why) *why = s;
        return false;
    };
    if (a.grid.w != b.grid.w || a.grid.h != b.grid.h) return fail("grid size");
    if (firstDiff("grid.terrain", a.grid.terrain, b.grid.terrain, why) ||
        firstDiff("grid.resType", a.grid.resType, b.grid.resType, why) ||
        firstDiff("grid.resAmount", a.grid.resAmount, b.grid.resAmount, why) ||
        firstDiff("grid.designated", a.grid.designated, b.grid.designated, why))
        return false;
    const Settlers& s = a.settlers;
    const Settlers& t = b.settlers;
    if (firstDiff("settlers.id", s.id, t.id, why) || firstDiff("settlers.x", s.x, t.x, why) ||
        firstDiff("settlers.y", s.y, t.y, why) || firstDiff("settlers.task", s.task, t.task, why) ||
        firstDiff("settlers.tx", s.tx, t.tx, why) || firstDiff("settlers.ty", s.ty, t.ty, why) ||
        firstDiff("settlers.carry", s.carry, t.carry, why) ||
        firstDiff("settlers.carryType", s.carryType, t.carryType, why) ||
        firstDiff("settlers.hunger", s.hunger, t.hunger, why) ||
        firstDiff("settlers.patience", s.patience, t.patience, why))
        return false;
    const Buildings& p = a.buildings;
    const Buildings& q = b.buildings;
    if (firstDiff("buildings.id", p.id, q.id, why) || firstDiff("buildings.kind", p.kind, q.kind, why) ||
        firstDiff("buildings.x", p.x, q.x, why) || firstDiff("buildings.y", p.y, q.y, why) ||
        firstDiff("buildings.builtTick", p.builtTick, q.builtTick, why))
        return false;
    const Globals& g = a.globals;
    const Globals& k = b.globals;
    const int64_t ga[] = {g.tick, g.rng, g.wood, g.stone, g.food, g.stockX, g.stockY,
                          g.maxSettlers, g.nextSettlerId, g.nextBuildingId, g.seed};
    const int64_t kb[] = {k.tick, k.rng, k.wood, k.stone, k.food, k.stockX, k.stockY,
                          k.maxSettlers, k.nextSettlerId, k.nextBuildingId, k.seed};
    const char* names[] = {"tick", "rng", "wood", "stone", "food", "stockX", "stockY",
                           "maxSettlers", "nextSettlerId", "nextBuildingId", "seed"};
    for (size_t i = 0; i < sizeof ga / sizeof ga[0]; ++i)
        if (ga[i] != kb[i]) return fail(std::string("globals.") + names[i]);
    if (a.season != b.season) return fail("season");
    return true;
}

uint64_t fingerprint(const Colony& c) {
    Fnv f;
    f.i64(c.grid.w);
    f.i64(c.grid.h);
    f.vec(c.grid.terrain);
    f.vec(c.grid.resType);
    f.vec(c.grid.resAmount);
    f.vec(c.grid.designated);
    const Settlers& s = c.settlers;
    f.vec(s.id);
    for (auto* col : {&s.x, &s.y, &s.task, &s.tx, &s.ty, &s.carry, &s.carryType, &s.hunger, &s.patience})
        f.vec(*col);
    const Buildings& b = c.buildings;
    f.vec(b.id);
    for (auto* col : {&b.kind, &b.x, &b.y, &b.builtTick}) f.vec(*col);
    const Globals& g = c.globals;
    for (int64_t v : {g.tick, g.rng, g.wood, g.stone, g.food, g.stockX, g.stockY, g.maxSettlers,
                      g.nextSettlerId, g.nextBuildingId, g.seed})
        f.i64(v);
    f.i64(static_cast<int64_t>(c.season));
    return f.h;
}

}  // namespace steading
