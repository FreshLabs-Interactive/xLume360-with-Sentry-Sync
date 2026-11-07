// xLume + Sentry - game names for the "Now Playing" screen (no SD card needed)
//
// Two sources, checked in this order:
//   1. Custom titles you add on the web page (stored in the ESP32's flash, /custom.txt)
//   2. The built-in list (game_table.h, generated from docs/gamelist.txt)
//
// Plain C++ (no Arduino calls) so the host tests can run it. Saving/loading the
// custom file is done by the caller (sentry.cpp) with LittleFS.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace gamedb {

const size_t kMaxText = 63;          // longest game / developer name kept for a custom title
const size_t kMaxCustom = 1000;      // custom titles (each line is ~50 bytes; flash holds ~100 KB)

struct Title {
    uint32_t id = 0;
    std::string name;
    std::string dev;
};

enum Source { NONE = 0, BUILTIN, CUSTOM };

// "4d5307e6" / "4D5307E6" / "0x4D5307E6" -> id. False if it isn't 8 hex digits.
bool ParseTitleId(const char* text, uint32_t* id);
void FormatTitleId(uint32_t id, char out[9]);               // "4D5307E6"

// Removes '*', line breaks and control characters, trims, and caps the length.
std::string Clean(const std::string& s);

uint32_t BuiltinCount();
bool BuiltinFind(uint32_t id, const char** name, const char** dev);

class Custom {
public:
    // Replaces the list with the contents of a file in gamelist.txt format.
    // Returns how many titles were read (bad lines are skipped).
    size_t Load(const std::string& text);
    std::string Save() const;                                // gamelist.txt format, sorted by ID

    const Title* Find(uint32_t id) const;
    bool Set(uint32_t id, const std::string& name, const std::string& dev);   // add or replace
    bool Remove(uint32_t id);
    const std::vector<Title>& All() const { return titles_; }

private:
    std::vector<Title> titles_;                              // kept sorted by id
};

// Looks up a title: custom first, then built in.
Source Lookup(const Custom& custom, uint32_t id, std::string* name, std::string* dev);

} // namespace gamedb
