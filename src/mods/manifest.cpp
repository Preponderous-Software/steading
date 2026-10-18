#include "manifest.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace steading::mods {

namespace {

void setErr(std::string* e, const std::string& s) {
    if (e) *e = s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Parses a basic TOML string starting at s[i] == '"'. Supports \" and \\.
bool parseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        const char ch = s[i++];
        if (ch == '"') return true;
        if (ch == '\\') {
            if (i >= s.size()) return false;
            const char esc = s[i++];
            if (esc != '"' && esc != '\\') return false;
            out += esc;
        } else if (static_cast<unsigned char>(ch) < 0x20) {
            return false;
        } else {
            out += ch;
        }
    }
    return false;  // unterminated
}

void skipSpace(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
}

bool restIsComment(const std::string& s, size_t i) {
    skipSpace(s, i);
    return i >= s.size() || s[i] == '#';
}

bool parseStringArray(const std::string& s, size_t& i, std::vector<std::string>& out) {
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    out.clear();
    skipSpace(s, i);
    if (i < s.size() && s[i] == ']') {
        ++i;
        return true;
    }
    for (;;) {
        skipSpace(s, i);
        std::string v;
        if (!parseString(s, i, v)) return false;
        out.push_back(v);
        skipSpace(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            skipSpace(s, i);
            if (i < s.size() && s[i] == ']') {  // trailing comma
                ++i;
                return true;
            }
            continue;
        }
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        return false;
    }
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char ch : id)
        if (!(std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) ||
              ch == '_' || ch == '-'))
            return false;
    return true;
}

}  // namespace

bool safeLayerPath(const std::string& p, std::string* why) {
    auto no = [&](const std::string& r) {
        setErr(why, "unsafe layer path \"" + p + "\": " + r);
        return false;
    };
    if (p.empty()) return no("empty");
    if (p[0] == '/') return no("absolute");
    if (p.find('\\') != std::string::npos) return no("backslash");
    if (p.find(':') != std::string::npos) return no("drive letter or URL scheme");
    if (p.find('\0') != std::string::npos) return no("NUL byte");
    std::stringstream ss(p);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) return no("empty path segment");
        if (seg == "..") return no("parent directory");
        if (seg == ".") return no("dot segment");
    }
    if (p.back() == '/') return no("trailing slash");
    auto endsWith = [&](const char* ext) {
        const std::string e(ext);
        return p.size() > e.size() && p.compare(p.size() - e.size(), e.size(), e) == 0;
    };
    if (!endsWith(".usda") && !endsWith(".usdc") && !endsWith(".usd")) return no("not a .usda/.usdc/.usd file");
    return true;
}

bool parse(const std::string& text, Manifest& out, std::string* err) {
    out = Manifest();
    std::set<std::string> seen;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) {
        setErr(err, "mod.toml line " + std::to_string(lineNo) + ": " + m);
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) return fail("expected key = value");
        const std::string key = trim(t.substr(0, eq));
        if (!seen.insert(key).second) return fail("duplicate key " + key);
        std::string rest = t.substr(eq + 1);
        size_t i = 0;
        skipSpace(rest, i);
        auto str = [&](std::string& dst) {
            if (!parseString(rest, i, dst) || !restIsComment(rest, i)) return fail("bad string for " + key);
            return true;
        };
        auto arr = [&](std::vector<std::string>& dst) {
            if (!parseStringArray(rest, i, dst) || !restIsComment(rest, i)) return fail("bad array for " + key);
            return true;
        };
        if (key == "id") { if (!str(out.id)) return false; }
        else if (key == "name") { if (!str(out.name)) return false; }
        else if (key == "version") { if (!str(out.version)) return false; }
        else if (key == "game") { if (!str(out.game)) return false; }
        else if (key == "schema") {
            size_t j = i;
            while (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) ++j;
            if (j == i || j - i > 6 || !restIsComment(rest, j)) return fail("schema must be a positive integer");
            out.schema = std::stoi(rest.substr(i, j - i));
            if (out.schema <= 0) return fail("schema must be a positive integer");
        }
        else if (key == "layers") { if (!arr(out.layers)) return false; }
        else if (key == "requires") { if (!arr(out.required)) return false; }
        else if (key == "conflicts") { if (!arr(out.conflicts)) return false; }
        else return fail("unknown key " + key);
    }
    lineNo = 0;
    for (const char* k : {"id", "name", "version", "game", "schema", "layers"})
        if (!seen.count(k)) return fail(std::string("missing required key ") + k);
    if (!validId(out.id)) return fail("id must match [a-z0-9_-]{1,64}");
    if (out.layers.empty()) return fail("layers must list at least one layer");
    std::string why;
    for (const auto& l : out.layers)
        if (!safeLayerPath(l, &why)) return fail(why);
    for (const auto& r : out.required)
        if (!validId(r)) return fail("bad mod id in requires: " + r);
    for (const auto& c : out.conflicts)
        if (!validId(c)) return fail("bad mod id in conflicts: " + c);
    return true;
}

bool loadDir(const std::string& dir, Manifest& out, std::string* err) {
    std::ifstream f(dir + "/mod.toml", std::ios::binary);
    if (!f) {
        setErr(err, "no mod.toml in " + dir);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string e;
    if (!parse(ss.str(), out, &e)) {
        setErr(err, dir + ": " + e);
        return false;
    }
    out.dir = dir;
    return true;
}

bool resolve(const std::vector<Manifest>& order, const std::string& gameId, int schemaVersion, Resolved& out,
             std::vector<std::string>& errors) {
    out = Resolved();
    errors.clear();
    std::map<std::string, size_t> position;
    for (size_t i = 0; i < order.size(); ++i) {
        const Manifest& m = order[i];
        if (!position.emplace(m.id, i).second) errors.push_back("mod " + m.id + " is listed twice");
        if (m.game != gameId) errors.push_back("mod " + m.id + " is for game \"" + m.game + "\", not " + gameId);
        if (m.schema != schemaVersion)
            errors.push_back("mod " + m.id + " targets schema " + std::to_string(m.schema) + ", game is at " +
                             std::to_string(schemaVersion));
    }
    for (size_t i = 0; i < order.size(); ++i) {
        const Manifest& m = order[i];
        for (const auto& r : m.required) {
            auto it = position.find(r);
            if (it == position.end()) errors.push_back("mod " + m.id + " requires " + r + ", which is not loaded");
            else if (it->second > i) errors.push_back("mod " + m.id + " requires " + r + " to load before it");
        }
        for (const auto& c : m.conflicts)
            if (position.count(c)) errors.push_back("mod " + m.id + " conflicts with " + c);
    }
    if (!errors.empty()) return false;
    for (const Manifest& m : order) {
        out.ids.push_back(m.id);
        for (const auto& l : m.layers) out.layerPaths.push_back(m.dir.empty() ? l : m.dir + "/" + l);
    }
    return true;
}

}  // namespace steading::mods
