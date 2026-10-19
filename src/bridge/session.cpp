#include "session.h"

#include <algorithm>
#include <filesystem>

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/variantSets.h>

#include "bridge.h"
#include "colony/schema.h"
#include "strata/usd/world_io.h"

namespace steading {

namespace {

void setErr(std::string* e, const std::string& s) {
    if (e) *e = s;
}

void readColor(const pxr::UsdStageRefPtr& st, const char* prim, float out[3]) {
    pxr::VtVec3fArray c;
    if (st->GetPrimAtPath(pxr::SdfPath(prim)).GetAttribute(pxr::TfToken("primvars:displayColor")).Get(&c) &&
        !c.empty())
        for (int i = 0; i < 3; ++i) out[i] = c[0][i];
}

bool readLooks(const std::vector<std::string>& stack, Looks& l, std::string* err) {
    auto st = strata::usd::compose(stack, err);
    if (!st) return false;
    readColor(st, "/Prefabs/Settler/Geom", l.settler);
    readColor(st, "/Prefabs/Hut/Geom", l.hut);
    readColor(st, "/Prefabs/HutPlan/Geom", l.hutPlan);
    readColor(st, "/Prefabs/Stockpile/Geom", l.stockpile);
    pxr::UsdPrim tree = st->GetPrimAtPath(pxr::SdfPath("/Prefabs/Tree"));
    if (tree) {
        pxr::UsdVariantSet vs = tree.GetVariantSet(schema::kSeasonSet);
        pxr::UsdEditContext ctx(st, st->GetSessionLayer());
        vs.SetVariantSelection("summer");
        readColor(st, "/Prefabs/Tree/Geom", l.treeSummer);
        vs.SetVariantSelection("winter");
        readColor(st, "/Prefabs/Tree/Geom", l.treeWinter);
        vs.ClearVariantSelection();
    }
    return true;
}

}  // namespace

std::vector<mods::Manifest> discoverMods(const std::string& gameDir, std::vector<std::string>* errors) {
    std::vector<mods::Manifest> out;
    const std::string dir = gameDir + "/mods";
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end; !ec && it != end;
         it.increment(ec))
        names.push_back(it->path().filename().u8string());
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
        mods::Manifest m;
        std::string err;
        if (mods::loadDir(dir + "/" + n, m, &err)) out.push_back(m);
        else if (errors) errors->push_back(err);
    }
    return out;
}

std::unique_ptr<Session> Session::open(const Options& opt, std::string* err) {
    std::unique_ptr<Session> s(new Session());
    s->opt_ = opt;
    std::vector<std::string> discoverErrors;
    const auto available = discoverMods(opt.gameDir, &discoverErrors);
    std::vector<mods::Manifest> order;
    for (const auto& id : opt.modOrder) {
        auto it = std::find_if(available.begin(), available.end(), [&](const mods::Manifest& m) { return m.id == id; });
        if (it == available.end()) {
            setErr(err, "mod " + id + " is not installed" +
                            (discoverErrors.empty() ? std::string() : " (" + discoverErrors[0] + ")"));
            return nullptr;
        }
        order.push_back(*it);
    }
    mods::Resolved resolved;
    std::vector<std::string> problems;
    if (!mods::resolve(order, schema::kGameId, schema::kSchemaVersion, resolved, problems)) {
        std::string all;
        for (const auto& p : problems) all += p + "; ";
        setErr(err, "mod load order: " + all);
        return nullptr;
    }
    s->modIds_ = resolved.ids;
    s->mods_ = bridge::modRefs(order);
    s->content_ = bridge::contentLayers(opt.gameDir);
    if (!bridge::readStats(s->content_, s->mods_, s->stats_, err)) return nullptr;
    std::vector<std::string> stack = s->content_;
    for (const auto& m : s->mods_) stack.insert(stack.end(), m.layers.begin(), m.layers.end());
    if (!readLooks(stack, s->looks_, err)) return nullptr;
    strata::save::Layers layers;
    layers.content = s->content_;
    layers.mods = s->mods_;
    s->slot_ = strata::save::Slot::open(opt.slotDir, bridge::saveSchema(), layers, err);
    if (!s->slot_) return nullptr;
    return s;
}

bool Session::hasGame() const { return strata::save::fs::exists(strata::save::fs::join(opt_.slotDir, "HEAD")); }

bool Session::newGame(const GenParams& p, std::string* err) {
    Colony c = generate(p);
    if (!slot_->create(bridge::toWorld(c), err)) return false;
    auto r = load();
    if (!r.ok) {
        setErr(err, r.error);
        return false;
    }
    std::string why;
    if (!equal(colony_, c, &why)) {
        setErr(err, "new game did not round-trip: " + why);
        return false;
    }
    return true;
}

strata::save::LoadResult Session::load(const strata::save::LoadOptions& opt) {
    auto r = slot_->load(opt);
    if (!r.ok) return r;
    Colony c;
    std::string why;
    if (!bridge::fromWorld(r.current, c, &why)) {
        r.ok = false;
        r.error = "save holds a colony the rules cannot run: " + why;
        return r;
    }
    colony_ = std::move(c);
    derived_ = derive(colony_);
    return r;
}

strata::save::SaveResult Session::save() { return slot_->save(bridge::toWorld(colony_), colony_.globals.tick); }

std::future<strata::save::SaveResult> Session::saveAsync() {
    return slot_->saveAsync(bridge::toWorld(colony_), colony_.globals.tick);
}

}  // namespace steading
