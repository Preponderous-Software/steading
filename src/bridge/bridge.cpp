#include "bridge.h"

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/variantSets.h>

#include "colony/schema.h"
#include "colony/view.h"
#include "strata/usd/world_io.h"

namespace steading::bridge {

using strata::world::FieldType;
using strata::world::Table;
using strata::world::World;

namespace {

constexpr size_t kSettlerFieldCount = sizeof(schema::kSettlerFields) / sizeof(schema::kSettlerFields[0]);
constexpr size_t kBuildingFieldCount = sizeof(schema::kBuildingFields) / sizeof(schema::kBuildingFields[0]);
constexpr size_t kGlobalCount = sizeof(schema::kGlobals) / sizeof(schema::kGlobals[0]);

std::string seasonKey() { return std::string(schema::kSeasonPrim) + "|" + schema::kSeasonSet; }

// Settler columns in schema::kSettlerFields order.
std::vector<const std::vector<int32_t>*> settlerCols(const Settlers& s) {
    return {&s.x, &s.y, &s.task, &s.tx, &s.ty, &s.carry, &s.carryType, &s.hunger, &s.patience};
}
std::vector<std::vector<int32_t>*> settlerCols(Settlers& s) {
    return {&s.x, &s.y, &s.task, &s.tx, &s.ty, &s.carry, &s.carryType, &s.hunger, &s.patience};
}
std::vector<const int64_t*> globalRefs(const Globals& g) {
    return {&g.tick,        &g.rng,    &g.wood,        &g.stone,          &g.food, &g.stockX,
            &g.stockY,      &g.maxSettlers, &g.nextSettlerId, &g.nextBuildingId, &g.seed};
}
std::vector<int64_t*> globalRefs(Globals& g) {
    return {&g.tick,        &g.rng,    &g.wood,        &g.stone,          &g.food, &g.stockX,
            &g.stockY,      &g.maxSettlers, &g.nextSettlerId, &g.nextBuildingId, &g.seed};
}
std::vector<const std::vector<int32_t>*> channels(const Grid& g) {
    return {&g.terrain, &g.resType, &g.resAmount, &g.designated};
}
std::vector<std::vector<int32_t>*> channels(Grid& g) { return {&g.terrain, &g.resType, &g.resAmount, &g.designated}; }

bool kindFromPrefab(const std::string& p, BuildingKind& k) {
    for (BuildingKind c : {BuildingKind::Stockpile, BuildingKind::Hut, BuildingKind::HutPlan})
        if (p == prefabFor(c)) {
            k = c;
            return true;
        }
    return false;
}

void setErr(std::string* e, const std::string& s) {
    if (e) *e = s;
}

}  // namespace

World schemaWorld() {
    World w;
    std::vector<std::string> ch(std::begin(schema::kGridChannels), std::end(schema::kGridChannels));
    w.grid.init(0, 0, 16, ch);
    Table s;
    s.collection = schema::kSettlers;
    s.idPrefix = schema::kSettlerPrefix;
    for (const char* f : schema::kSettlerFields) s.fields.push_back({f, FieldType::Int});
    s.initColumns();
    Table b;
    b.collection = schema::kBuildings;
    b.idPrefix = schema::kBuildingPrefix;
    for (const char* f : schema::kBuildingFields) b.fields.push_back({f, FieldType::Int});
    b.initColumns();
    w.tables = {s, b};
    w.variants[seasonKey()] = "";
    return w;
}

World toWorld(const Colony& c) {
    World w = schemaWorld();
    w.grid.init(c.grid.w, c.grid.h, 16, w.grid.channels);
    const auto ch = channels(c.grid);
    for (size_t i = 0; i < ch.size(); ++i) w.grid.data[i] = *ch[i];
    Table& S = w.tables[0];
    S.ids = c.settlers.id;
    S.prefab.assign(c.settlers.size(), "/Prefabs/Settler");
    const auto sc = settlerCols(c.settlers);
    for (size_t f = 0; f < kSettlerFieldCount; ++f) S.ints[f].assign(sc[f]->begin(), sc[f]->end());
    Table& B = w.tables[1];
    B.ids = c.buildings.id;
    B.prefab.resize(c.buildings.size());
    for (size_t r = 0; r < c.buildings.size(); ++r)
        B.prefab[r] = prefabFor(static_cast<BuildingKind>(c.buildings.kind[r]));
    B.ints[0].assign(c.buildings.x.begin(), c.buildings.x.end());
    B.ints[1].assign(c.buildings.y.begin(), c.buildings.y.end());
    B.ints[2].assign(c.buildings.builtTick.begin(), c.buildings.builtTick.end());
    const auto g = globalRefs(c.globals);
    for (size_t i = 0; i < kGlobalCount; ++i) w.globals[schema::kGlobals[i]] = *g[i];
    w.variants[seasonKey()] = c.season == Season::Winter ? "winter" : "summer";
    return w;
}

bool fromWorld(const World& w, Colony& out, std::string* err) {
    Colony c;
    if (w.grid.w <= 0 || w.grid.h <= 0) return setErr(err, "empty grid"), false;
    c.grid.init(w.grid.w, w.grid.h);
    const auto ch = channels(c.grid);
    if (w.grid.data.size() != ch.size()) return setErr(err, "grid channel count"), false;
    for (size_t i = 0; i < ch.size(); ++i) *ch[i] = w.grid.data[i];
    if (w.tables.size() != 2 || w.tables[0].fields.size() != kSettlerFieldCount ||
        w.tables[1].fields.size() != kBuildingFieldCount)
        return setErr(err, "table layout"), false;
    auto fits = [](int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; };
    const Table& S = w.tables[0];
    c.settlers.id = S.ids;
    const auto sc = settlerCols(c.settlers);
    for (size_t f = 0; f < kSettlerFieldCount; ++f) {
        sc[f]->reserve(S.size());
        for (int64_t v : S.ints[f]) {
            if (!fits(v)) return setErr(err, "settler field out of range"), false;
            sc[f]->push_back(static_cast<int32_t>(v));
        }
    }
    const Table& B = w.tables[1];
    c.buildings.id = B.ids;
    for (size_t r = 0; r < B.size(); ++r) {
        BuildingKind k;
        if (!kindFromPrefab(B.prefab[r], k)) return setErr(err, "unknown building prefab " + B.prefab[r]), false;
        for (size_t f = 0; f < kBuildingFieldCount; ++f)
            if (!fits(B.ints[f][r])) return setErr(err, "building field out of range"), false;
        c.buildings.kind.push_back(static_cast<int32_t>(k));
        c.buildings.x.push_back(static_cast<int32_t>(B.ints[0][r]));
        c.buildings.y.push_back(static_cast<int32_t>(B.ints[1][r]));
        c.buildings.builtTick.push_back(static_cast<int32_t>(B.ints[2][r]));
    }
    const auto g = globalRefs(c.globals);
    for (size_t i = 0; i < kGlobalCount; ++i) {
        auto it = w.globals.find(schema::kGlobals[i]);
        if (it == w.globals.end()) return setErr(err, std::string("missing global ") + schema::kGlobals[i]), false;
        *g[i] = it->second;
    }
    auto sv = w.variants.find(seasonKey());
    if (sv == w.variants.end() || (sv->second != "summer" && sv->second != "winter"))
        return setErr(err, "season variant must be summer or winter"), false;
    c.season = sv->second == "winter" ? Season::Winter : Season::Summer;
    // Range checks the rules rely on.
    for (size_t i = 0; i < c.settlers.size(); ++i)
        if (!c.grid.inBounds(c.settlers.x[i], c.settlers.y[i]) || c.settlers.task[i] < 0 || c.settlers.task[i] > 3)
            return setErr(err, "settler state out of range"), false;
    for (size_t i = 0; i < c.buildings.size(); ++i)
        if (!c.grid.inBounds(c.buildings.x[i], c.buildings.y[i])) return setErr(err, "building out of bounds"), false;
    if (!c.grid.inBounds(static_cast<int32_t>(c.globals.stockX), static_cast<int32_t>(c.globals.stockY)))
        return setErr(err, "stockpile out of bounds"), false;
    out = std::move(c);
    return true;
}

strata::save::Schema saveSchema() {
    strata::save::Schema s;
    s.gameId = schema::kGameId;
    s.version = schema::kSchemaVersion;
    s.templateFor = [](int version) {
        if (version != schema::kSchemaVersion) throw std::runtime_error("no steading schema " + std::to_string(version));
        return schemaWorld();
    };
    return s;
}

std::vector<std::string> contentLayers(const std::string& gameDir) { return {gameDir + "/content/prefabs.usda"}; }

std::vector<strata::save::ModRef> modRefs(const std::vector<mods::Manifest>& order) {
    std::vector<strata::save::ModRef> out;
    for (const auto& m : order) {
        strata::save::ModRef r;
        r.id = m.id;
        for (const auto& l : m.layers) r.layers.push_back(m.dir.empty() ? l : m.dir + "/" + l);
        out.push_back(std::move(r));
    }
    return out;
}

bool readStats(const std::vector<std::string>& content, const std::vector<strata::save::ModRef>& mods, Stats& s,
               std::string* err) {
    std::vector<std::string> stack = content;
    for (const auto& m : mods) stack.insert(stack.end(), m.layers.begin(), m.layers.end());
    auto stage = strata::usd::compose(stack, err);
    if (!stage) return false;
    using namespace pxr;
    Stats out;
    UsdPrim hut = stage->GetPrimAtPath(SdfPath("/Prefabs/Hut"));
    UsdPrim tree = stage->GetPrimAtPath(SdfPath("/Prefabs/Tree"));
    if (!hut || !tree) return setErr(err, "content has no /Prefabs/Hut or /Prefabs/Tree"), false;
    bool ok = hut.GetAttribute(TfToken("colony:stats:woodCost")).Get(&out.hutWoodCost) &&
              hut.GetAttribute(TfToken("colony:stats:capacity")).Get(&out.hutCapacity);
    // Seasons are variants: read the stat under each selection through the
    // session layer, then clear the session edit.
    UsdVariantSet vs = tree.GetVariantSet(schema::kSeasonSet);
    {
        UsdEditContext ctx(stage, stage->GetSessionLayer());
        ok = ok && vs.SetVariantSelection("summer") &&
             tree.GetAttribute(TfToken("colony:stats:regrowPerMille")).Get(&out.regrowSummer);
        ok = ok && vs.SetVariantSelection("winter") &&
             tree.GetAttribute(TfToken("colony:stats:regrowPerMille")).Get(&out.regrowWinter);
        vs.ClearVariantSelection();
    }
    if (!ok) return setErr(err, "a prefab stat is missing or mistyped"), false;
    if (out.hutWoodCost < 1 || out.hutWoodCost > 1e6 || out.hutCapacity < 0 || out.hutCapacity > 1e6 ||
        out.regrowSummer < 0 || out.regrowSummer > 1000 || out.regrowWinter < 0 || out.regrowWinter > 1000)
        return setErr(err, "a prefab stat is out of range"), false;
    s = out;
    return true;
}

}  // namespace steading::bridge
