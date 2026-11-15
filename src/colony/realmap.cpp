#include "realmap.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>

#include "rng.h"
#include "stats.h"

// stb_image, PNG only, private to this file (STB_IMAGE_STATIC keeps its symbols
// from clashing with another copy linked into the game).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace steading {

namespace {

// ------------------------------------------------------------ a small JSON reader
// Enough of JSON for world.json: objects, arrays, strings, numbers, true, false, null.
struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json* get(const std::string& k) const {
        auto it = fields.find(k);
        return kind == Object && it != fields.end() ? &it->second : nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Json& out, std::string& err) {
        if (!value(out, 0)) {
            err = "world.json: " + why_ + " at byte " + std::to_string(i_);
            return false;
        }
        ws();
        if (i_ != s_.size()) {
            err = "world.json: trailing data at byte " + std::to_string(i_);
            return false;
        }
        return true;
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    std::string why_;

    void ws() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    bool fail(const char* why) {
        why_ = why;
        return false;
    }
    bool literal(const char* word) {
        const std::string w(word);
        if (s_.compare(i_, w.size(), w) != 0) return fail("bad literal");
        i_ += w.size();
        return true;
    }
    bool str(std::string& out) {
        ++i_;  // opening quote
        while (i_ < s_.size() && s_[i_] != '"') {
            char ch = s_[i_++];
            if (ch == '\\') {
                if (i_ >= s_.size()) return fail("bad escape");
                const char e = s_[i_++];
                switch (e) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case 'b': ch = '\b'; break;
                    case 'f': ch = '\f'; break;
                    case 'u':  // kept only for ASCII; other code points become '?'
                        if (i_ + 4 > s_.size()) return fail("bad escape");
                        {
                            const long cp = std::strtol(s_.substr(i_, 4).c_str(), nullptr, 16);
                            ch = cp < 128 ? static_cast<char>(cp) : '?';
                        }
                        i_ += 4;
                        break;
                    default: ch = e;
                }
            }
            out += ch;
        }
        if (i_ >= s_.size()) return fail("unterminated string");
        ++i_;
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nested too deeply");
        ws();
        if (i_ >= s_.size()) return fail("unexpected end");
        const char ch = s_[i_];
        if (ch == '{') {
            v.kind = Json::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') return ++i_, true;
            for (;;) {
                ws();
                if (i_ >= s_.size() || s_[i_] != '"') return fail("expected a key");
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
                ++i_;
                if (!value(v.fields[k], depth + 1)) return false;
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') return ++i_, true;
                return fail("expected ',' or '}'");
            }
        }
        if (ch == '[') {
            v.kind = Json::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') return ++i_, true;
            for (;;) {
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') return ++i_, true;
                return fail("expected ',' or ']'");
            }
        }
        if (ch == '"') {
            v.kind = Json::String;
            return str(v.text);
        }
        if (ch == 't') { v.kind = Json::Bool; v.number = 1; return literal("true"); }
        if (ch == 'f') { v.kind = Json::Bool; return literal("false"); }
        if (ch == 'n') return literal("null");
        const char* start = s_.c_str() + i_;
        char* end = nullptr;
        v.number = std::strtod(start, &end);
        if (end == start) return fail("unexpected character");
        v.kind = Json::Number;
        i_ += static_cast<size_t>(end - start);
        return true;
    }
};

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool fail(std::string* err, const std::string& why) {
    if (err) *err = why;
    return false;
}

// A raster's file name from world.json `files`: a plain name inside the package.
bool fileName(const Json& root, const char* key, const char* fallback, std::string& out, std::string* err) {
    out = fallback;
    if (const Json* files = root.get("files"))
        if (const Json* f = files->get(key)) {
            if (f->kind != Json::String) return fail(err, std::string("world.json: files.") + key + " is not a string");
            out = f->text;
        }
    if (out.empty() || out.find_first_of("/\\") != std::string::npos || out == "." || out == "..")
        return fail(err, std::string("world.json: files.") + key + " must be a file name inside the package");
    return true;
}

struct Image {
    int w = 0, h = 0;
    std::unique_ptr<void, void (*)(void*)> px{nullptr, stbi_image_free};
};

bool readPng(const std::string& path, bool sixteen, Image& img, std::string* err) {
    std::string bytes;
    if (!readFile(path, bytes)) return fail(err, "cannot read " + path);
    if (bytes.size() > 64u * 1024u * 1024u) return fail(err, path + " is too large");
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int len = static_cast<int>(bytes.size());
    int channels = 0;
    void* px = sixteen ? static_cast<void*>(stbi_load_16_from_memory(data, len, &img.w, &img.h, &channels, 1))
                       : static_cast<void*>(stbi_load_from_memory(data, len, &img.w, &img.h, &channels, 1));
    if (!px) return fail(err, path + ": " + stbi_failure_reason());
    img.px.reset(px);
    return true;
}

constexpr int32_t kGrass = static_cast<int32_t>(Terrain::Grass);
constexpr int32_t kWater = static_cast<int32_t>(Terrain::Water);
constexpr int32_t kRock = static_cast<int32_t>(Terrain::Rock);

}  // namespace

bool readMapPackage(const std::string& dir, MapPackage& out, std::string* err) {
    const std::string base = dir.empty() ? std::string(".") : dir;
    auto join = [&](const std::string& name) { return base + "/" + name; };
    std::string text;
    if (!readFile(join("world.json"), text)) return fail(err, "no world.json in " + base);
    Json root;
    std::string why;
    if (!Parser(text).parse(root, why)) return fail(err, why);
    if (root.kind != Json::Object) return fail(err, "world.json is not an object");
    const Json* grid = root.get("grid");
    const Json* gw = grid ? grid->get("width") : nullptr;
    const Json* gh = grid ? grid->get("height") : nullptr;
    if (!gw || !gh || gw->kind != Json::Number || gh->kind != Json::Number)
        return fail(err, "world.json: grid.width and grid.height are required");
    if (gw->number < 1 || gh->number < 1 || gw->number > kMaxMapSide || gh->number > kMaxMapSide)
        return fail(err, "world.json: the grid is " + std::to_string(static_cast<long>(gw->number)) + "x" +
                             std::to_string(static_cast<long>(gh->number)) + " px; steading reads at most " +
                             std::to_string(kMaxMapSide) + "x" + std::to_string(kMaxMapSide) + " (crop it first)");
    const Json* classes = root.get("classes");
    if (!classes || classes->kind != Json::Array || classes->items.empty() || classes->items.size() > 256)
        return fail(err, "world.json: classes must be a list of 1 to 256 names");
    MapPackage m;
    m.w = static_cast<int32_t>(gw->number);
    m.h = static_cast<int32_t>(gh->number);
    for (const Json& c : classes->items) {
        if (c.kind != Json::String) return fail(err, "world.json: classes must be names");
        m.classes.push_back(c.text);
    }
    std::string heightName, groundName;
    if (!fileName(root, "height", "height.png", heightName, err)) return false;
    if (!fileName(root, "ground", "ground.png", groundName, err)) return false;
    Image height, ground;
    if (!readPng(join(heightName), true, height, err)) return false;
    if (!readPng(join(groundName), false, ground, err)) return false;
    if (height.w != m.w || height.h != m.h || ground.w != m.w || ground.h != m.h)
        return fail(err, "rasters do not match the grid (" + std::to_string(m.w) + "x" + std::to_string(m.h) +
                             "): height is " + std::to_string(height.w) + "x" + std::to_string(height.h) +
                             ", ground is " + std::to_string(ground.w) + "x" + std::to_string(ground.h));
    const size_t n = static_cast<size_t>(m.w) * static_cast<size_t>(m.h);
    const auto* hp = static_cast<const uint16_t*>(height.px.get());
    const auto* gp = static_cast<const uint8_t*>(ground.px.get());
    m.ground.assign(gp, gp + n);
    m.hasData.resize(n);
    for (size_t i = 0; i < n; ++i) {
        m.hasData[i] = hp[i] != 0;
        if (m.ground[i] >= m.classes.size())
            return fail(err, "ground.png has class " + std::to_string(m.ground[i]) + ", but world.json names only " +
                                 std::to_string(m.classes.size()));
    }
    out = std::move(m);
    return true;
}

Terrain terrainForClass(const std::string& name, bool* tree) {
    if (tree) *tree = name == "forest";
    if (name == "water" || name == "ice" || name == "void") return Terrain::Water;
    if (name == "stone" || name == "snow" || name == "lava") return Terrain::Rock;
    return Terrain::Grass;  // plain, forest, sand, badland, farm, built, and any class steading does not know
}

Colony importMap(const MapPackage& m, const GenParams& p) {
    if (m.w < 8 || m.h < 8) throw std::invalid_argument("map must be at least 8x8");
    if (m.w > kMaxMapSide || m.h > kMaxMapSide) throw std::invalid_argument("map is larger than steading reads (crop it first)");
    const size_t n = static_cast<size_t>(m.w) * static_cast<size_t>(m.h);
    if (m.ground.size() != n || m.hasData.size() != n) throw std::invalid_argument("map rasters do not match its size");
    if (p.settlers < 0) throw std::invalid_argument("settlers must be >= 0");
    Colony c;
    Grid& g = c.grid;
    g.init(m.w, m.h);
    std::vector<uint8_t> forest(n, 0);
    bool anyGrass = false;
    for (size_t i = 0; i < n; ++i) {
        bool tree = false;
        const Terrain t = m.hasData[i] && m.ground[i] < m.classes.size() ? terrainForClass(m.classes[m.ground[i]], &tree)
                                                                         : Terrain::Water;
        g.terrain[i] = static_cast<int32_t>(t);
        forest[i] = tree;
        anyGrass = anyGrass || t == Terrain::Grass;
    }
    if (!anyGrass) throw std::invalid_argument("map has no open ground to settle");

    // Resources, one roll per tile in row order so a package and seed always give
    // the same colony. Trees thin the forest enough to walk through; stone sits on
    // rock within two tiles of open ground (a mountain's flanks, not its heart).
    Rng rng{mix64(p.seed ^ 0x6D61705FULL)};
    auto nearOpen = [&](int32_t x, int32_t y) {
        for (int32_t dy = -2; dy <= 2; ++dy)
            for (int32_t dx = -2; dx <= 2; ++dx)
                if (g.inBounds(x + dx, y + dy) && g.terrain[g.at(x + dx, y + dy)] == kGrass) return true;
        return false;
    };
    for (int32_t y = 0; y < g.h; ++y)
        for (int32_t x = 0; x < g.w; ++x) {
            const size_t i = g.at(x, y);
            const int roll = rng.below(1000);
            if (g.terrain[i] == kRock) {
                if (roll < 300 && nearOpen(x, y)) {
                    g.resType[i] = static_cast<int32_t>(Resource::Stone);
                    g.resAmount[i] = Rules::kStoneAmount;
                }
            } else if (g.terrain[i] == kGrass) {
                if (forest[i] && roll < 550) {
                    g.resType[i] = static_cast<int32_t>(Resource::Tree);
                    g.resAmount[i] = Rules::kTreeAmount;
                } else if (roll < 25) {
                    g.resType[i] = static_cast<int32_t>(Resource::Berry);
                    g.resAmount[i] = Rules::kBerryAmount;
                }
            } else if (g.terrain[i] != kWater) {
                throw std::logic_error("unknown terrain");
            }
        }
    settle(c, p, rng);
    return c;
}

}  // namespace steading
