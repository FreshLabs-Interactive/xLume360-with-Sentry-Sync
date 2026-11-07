#include "gamedb.h"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "game_table.h"

namespace gamedb {

bool ParseTitleId(const char* text, uint32_t* id) {
    if (!text) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    uint32_t v = 0;
    int n = 0;
    for (; isxdigit((unsigned char)*text); ++text, ++n) {
        if (n == 8) return false;
        char c = (char)toupper((unsigned char)*text);
        v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    if (n != 8 || *text) return false;
    *id = v;
    return true;
}

void FormatTitleId(uint32_t id, char out[9]) { snprintf(out, 9, "%08X", (unsigned)id); }

std::string Clean(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '*' || c < 0x20 || c == 0x7F) c = ' ';
        out += (char)c;
    }
    size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return "";
    size_t b = out.find_last_not_of(' ');
    out = out.substr(a, b - a + 1);
    if (out.size() > kMaxText) out.resize(kMaxText);
    return out;
}

uint32_t BuiltinCount() { return kGameCount; }

bool BuiltinFind(uint32_t id, const char** name, const char** dev) {
    const GameIndexEntry* end = kGameIndex + kGameCount;
    const GameIndexEntry* e = std::lower_bound(kGameIndex, end, id,
        [](const GameIndexEntry& a, uint32_t v) { return a.id < v; });
    if (e == end || e->id != id) return false;
    *name = kGameText + e->text;
    *dev = *name + strlen(*name) + 1;
    return true;
}

// ----------------------------------------------------------------- custom --
size_t Custom::Load(const std::string& text) {
    titles_.clear();
    size_t pos = 0;
    while (pos < text.size() && titles_.size() < kMaxCustom) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        size_t a = line.find('*');
        if (a == std::string::npos) continue;
        uint32_t id;
        if (!ParseTitleId(line.substr(0, a).c_str(), &id)) continue;
        size_t b = line.find('*', a + 1);
        std::string name = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        std::string dev = b == std::string::npos ? "" : line.substr(b + 1);
        Set(id, name, dev);
    }
    return titles_.size();
}

std::string Custom::Save() const {
    std::string out;
    char hex[9];
    for (const Title& t : titles_) {
        FormatTitleId(t.id, hex);
        out += hex; out += '*'; out += t.name; out += '*'; out += t.dev; out += '\n';
    }
    return out;
}

const Title* Custom::Find(uint32_t id) const {
    auto it = std::lower_bound(titles_.begin(), titles_.end(), id,
        [](const Title& a, uint32_t v) { return a.id < v; });
    return it != titles_.end() && it->id == id ? &*it : nullptr;
}

bool Custom::Set(uint32_t id, const std::string& name, const std::string& dev) {
    std::string n = Clean(name), d = Clean(dev);
    if (n.empty()) return false;
    auto it = std::lower_bound(titles_.begin(), titles_.end(), id,
        [](const Title& a, uint32_t v) { return a.id < v; });
    if (it != titles_.end() && it->id == id) {
        it->name = n;
        it->dev = d;
        return true;
    }
    if (titles_.size() >= kMaxCustom) return false;
    Title t;
    t.id = id; t.name = n; t.dev = d;
    titles_.insert(it, t);
    return true;
}

bool Custom::Remove(uint32_t id) {
    auto it = std::lower_bound(titles_.begin(), titles_.end(), id,
        [](const Title& a, uint32_t v) { return a.id < v; });
    if (it == titles_.end() || it->id != id) return false;
    titles_.erase(it);
    return true;
}

Source Lookup(const Custom& custom, uint32_t id, std::string* name, std::string* dev) {
    if (const Title* t = custom.Find(id)) {
        *name = t->name;
        *dev = t->dev;
        return CUSTOM;
    }
    const char *n, *d;
    if (BuiltinFind(id, &n, &d)) {
        *name = n;
        *dev = d;
        return BUILTIN;
    }
    return NONE;
}

} // namespace gamedb
