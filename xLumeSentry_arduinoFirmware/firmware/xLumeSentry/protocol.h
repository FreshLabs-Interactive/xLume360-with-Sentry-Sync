// xLume + Sentry - line parsers (plain C++, no Arduino dependencies, so they
// are unit-tested on a PC: see tests/test_protocol.cpp)
//
// The console prints text lines on its debug UART:
//   "[xLume] - <command>"   from the xLume 360 app / plugin  -> LED engine
//   "[Sentry] <data>"       from the xLume 360 plugin's Sentry reporter
//                           (and the old Sentry plugin)      -> status screen
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace xls {

// ---------------------------------------------------------------- xLume ----
// Same rules as the original xLume V2 firmware's parser (decoded from
// xLumeV2.bin), so the xLume app, plugin and old app all keep working.
enum XlCmdType { XL_NONE = 0, XL_COLOR, XL_COLOR2, XL_EFFECT, XL_BRIGHTNESS, XL_SPEED, XL_LEDCOUNT };

struct XlCommand {
    XlCmdType type;
    int a, b, c;      // colour r,g,b, or the single value in a
};

static const int XL_EFFECT_COUNT = 23;

// line: a full UART line. Returns the text after "[xLume] - " (whitespace
// trimmed off the start) or nullptr if it isn't an xLume line.
const char* XlBody(const char* line);

// body: what XlBody returned (or the same text from the web page).
bool XlParse(const char* body, XlCommand* out);

const char* XlEffectName(int index);

// --------------------------------------------------------------- Sentry ----
enum SentryType {
    SN_NONE = 0,
    SN_TEMP,         // index 0=CPU 1=GPU 2=eDRAM 3=Mobo, value
    SN_TITLEID,      // text = 8 hex digits
    SN_IP,           // text = dotted IP
    SN_MODE_UART,    // "[Sentry] UART Mode" or "[Sentry] $mode=2"
    SN_MODE_DEFAULT, // "[Sentry] Default Mode" or "[Sentry] $mode=1"
    SN_STARTED,      // "[Sentry] Sentry started!"
    SN_OTHER         // any other [Sentry] line
};

struct SentryEvent {
    SentryType type;
    int index;
    float value;
    char text[40];
};

// Returns false if the line isn't a Sentry line.
bool SentryParse(const char* line, SentryEvent* out);

// True for "[Sentry] CPU: 50.1"-style lines (kept off the UART-mode screen,
// exactly like the original SentrySync).
bool SentryIsTempLine(const char* line);

} // namespace xls
