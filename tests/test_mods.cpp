#include "check.h"
#include "mods/manifest.h"

using namespace steading::mods;

namespace {
Manifest mk(const std::string& id, std::vector<std::string> req = {}, std::vector<std::string> conf = {}) {
    Manifest m;
    m.id = id;
    m.name = id;
    m.version = "1.0.0";
    m.game = "steading";
    m.schema = 1;
    m.layers = {id + ".usda"};
    m.required = std::move(req);
    m.conflicts = std::move(conf);
    m.dir = "/mods/" + id;
    return m;
}
}  // namespace

int main() {
    SECTION("the bundled demo mods parse");
    for (const char* id : {"cheap_huts", "frost_huts"}) {
        Manifest m;
        std::string err;
        CHECK_MSG(loadDir(std::string(STEADING_SOURCE_DIR) + "/mods/" + id, m, &err), err);
        CHECK(m.id == id && m.game == "steading" && m.schema == 1 && m.layers.size() == 1);
    }
    SECTION("full manifest syntax");
    {
        Manifest m;
        std::string err;
        const char* text =
            "# comment\n"
            "id = \"granary\"   # trailing comment\n"
            "name = \"The \\\"Granary\\\"\"\n"
            "version = \"0.2.0\"\r\n"
            "game = \"steading\"\n"
            "schema = 1\n"
            "layers = [\"a.usda\", \"sub/b.usdc\",]\n"
            "requires = [\"cheap_huts\"]\n"
            "conflicts = []\n";
        CHECK_MSG(parse(text, m, &err), err);
        CHECK(m.name == "The \"Granary\"" && m.layers.size() == 2 && m.layers[1] == "sub/b.usdc");
        CHECK(m.required.size() == 1 && m.conflicts.empty());
    }
    SECTION("malformed manifests are rejected with a reason");
    {
        const std::string ok = "name = \"x\"\nversion = \"1\"\ngame = \"steading\"\nschema = 1\n";
        const char* bad[] = {
            "id = \"UPPER\"\nlayers = [\"a.usda\"]\n",          // bad id
            "id = \"a\"\nlayers = [\"a.usda\"]\nid = \"b\"\n",   // duplicate key
            "id = \"a\"\nlayers = [\"a.usda\"]\nscript = \"x\"\n",  // unknown key (no code mods)
            "id = \"a\"\nlayers = []\n",                         // no layers
            "id = \"a\"\nlayers = [\"a.usda\"\n",                // unterminated array
            "id = \"a\nlayers = [\"a.usda\"]\n",                 // unterminated string
            "id = \"a\"\n",                                      // missing layers
            "id = \"a\"\nlayers = [\"a.usda\"]\nrequires = [\"Bad Id\"]\n",
        };
        for (const char* b : bad) {
            Manifest m;
            std::string err;
            CHECK_MSG(!parse(ok + b, m, &err), std::string("accepted: ") + b);
            CHECK(!err.empty());
        }
        Manifest m;
        std::string err;
        CHECK(!parse("id = \"a\"\nlayers = [\"a.usda\"]\nschema = -1\nname=\"x\"\nversion=\"1\"\ngame=\"steading\"\n", m, &err));
        CHECK(!parse("id = \"a\"\nlayers = [\"a.usda\"]\nschema = 0\nname=\"x\"\nversion=\"1\"\ngame=\"steading\"\n", m, &err));
    }
    SECTION("layer paths cannot escape the mod directory");
    {
        for (const char* p : {"a.usda", "dir/b.usdc", "c.usd"}) CHECK_MSG(safeLayerPath(p), p);
        for (const char* p : {"", "/etc/passwd.usda", "../x.usda", "a/../../x.usda", "a\\b.usda", "C:/x.usda",
                              "http://evil/x.usda", "a//b.usda", "./a.usda", "a.txt", "dir/", "a.usda/"})
            CHECK_MSG(!safeLayerPath(p), std::string("accepted unsafe path: ") + p);
    }
    SECTION("load order: later mods win; layers come out weakest first");
    {
        Resolved r;
        std::vector<std::string> errs;
        CHECK(resolve({mk("frost"), mk("cheap")}, "steading", 1, r, errs));
        CHECK(r.ids == (std::vector<std::string>{"frost", "cheap"}));
        CHECK(r.layerPaths == (std::vector<std::string>{"/mods/frost/frost.usda", "/mods/cheap/cheap.usda"}));
        CHECK(resolve({}, "steading", 1, r, errs) && r.layerPaths.empty());
    }
    SECTION("every load-order problem is reported");
    {
        Resolved r;
        std::vector<std::string> errs;
        Manifest other = mk("other");
        other.game = "beyond-nations";
        Manifest old = mk("old");
        old.schema = 0;
        CHECK(!resolve({mk("a", {"b"}), mk("b"), mk("c", {"missing"}), mk("d", {}, {"a"}), mk("a"), other, old},
                       "steading", 1, r, errs));
        auto has = [&](const std::string& s) {
            for (auto& e : errs)
                if (e.find(s) != std::string::npos) return true;
            return false;
        };
        CHECK(has("requires b to load before it"));
        CHECK(has("requires missing, which is not loaded"));
        CHECK(has("conflicts with a"));
        CHECK(has("listed twice"));
        CHECK(has("is for game \"beyond-nations\""));
        CHECK(has("targets schema 0"));
        CHECK(r.layerPaths.empty());
    }
    return check::finish();
}
