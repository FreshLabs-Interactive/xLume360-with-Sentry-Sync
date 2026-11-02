#include "protocol.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

namespace xls {

namespace {

const char* SkipSpace(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

// Copies s into buf with leading/trailing whitespace removed.
void TrimCopy(const char* s, char* buf, size_t size) {
    s = SkipSpace(s);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) --n;
    if (n >= size) n = size - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
}

bool StartsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Firmware rule: optional '-', then digits only.
bool ParseIntStrict(const char* s, long* out) {
    char buf[24];
    TrimCopy(s, buf, sizeof(buf));
    if (!buf[0]) return false;
    const char* p = buf;
    if (*p == '-') ++p;
    if (!*p) return false;
    for (const char* q = p; *q; ++q)
        if (*q < '0' || *q > '9') return false;
    *out = strtol(buf, nullptr, 10);
    return true;
}

// Firmware rule: only digits, spaces, tabs, ',' and '-', and at least one digit.
bool ValidColorText(const char* s) {
    bool digit = false;
    for (; *s; ++s) {
        char c = *s;
        if (c >= '0' && c <= '9') digit = true;
        else if (!(c == ' ' || c == '\t' || c == ',' || c == '-' || c == '\r' || c == '\n')) return false;
    }
    return digit;
}

int Clamp(long v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : (int)v); }

bool ParseRgb(const char* s, int* r, int* g, int* b) {
    const char* c1 = strchr(s, ',');
    if (!c1) return false;
    const char* c2 = strchr(c1 + 1, ',');
    if (!c2) return false;
    *r = Clamp(atol(s), 0, 255);
    *g = Clamp(atol(c1 + 1), 0, 255);
    *b = Clamp(atol(c2 + 1), 0, 255);
    return true;
}

const char* const kEffectNames[XL_EFFECT_COUNT] = {
    "Static Color", "Static Gradient", "Static Pulse", "Static Twinkle", "LEDs Off",
    "Color Wave", "Color Cycle", "Dynamic Pulse", "Dynamic Twinkle", "Fire",
    "Confetti", "Sparkle", "Theater Chase", "Rainbow Chase", "Larson Scanner",
    "Comet", "Meteor Rain", "Color Wipe", "Two-Color Gradient", "Two-Color Chase",
    "Strobe", "Police Lights", "Candle Flicker"
};

} // namespace

const char* XlBody(const char* line) {
    static const char kPrefix[] = "[xLume] - ";
    line = SkipSpace(line);
    if (!StartsWith(line, kPrefix)) return nullptr;
    return SkipSpace(line + sizeof(kPrefix) - 1);
}

bool XlParse(const char* body, XlCommand* out) {
    char buf[96];
    TrimCopy(body, buf, sizeof(buf));
    out->type = XL_NONE;
    out->a = out->b = out->c = 0;
    if (!buf[0]) return false;
    long v;
    if (StartsWith(buf, "Effects:")) {
        // The original accepts 0..24; only 0..22 exist, so ignore the rest.
        if (ParseIntStrict(buf + 8, &v) && v >= 0 && v < XL_EFFECT_COUNT) {
            out->type = XL_EFFECT; out->a = (int)v; return true;
        }
        return false;
    }
    if (StartsWith(buf, "ledCount:")) {
        if (ParseIntStrict(buf + 9, &v) && v > 0) {
            out->type = XL_LEDCOUNT; out->a = Clamp(v, 1, 300); return true;
        }
        return false;
    }
    if (StartsWith(buf, "Brightness:")) {
        if (ParseIntStrict(buf + 11, &v)) { out->type = XL_BRIGHTNESS; out->a = Clamp(v, 0, 255); return true; }
        return false;
    }
    if (StartsWith(buf, "Speed:")) {
        if (ParseIntStrict(buf + 6, &v)) { out->type = XL_SPEED; out->a = Clamp(v, 1, 255); return true; }
        return false;
    }
    const char* rgb = nullptr;
    XlCmdType t = XL_COLOR;
    if (StartsWith(buf, "Color2:")) { rgb = buf + 7; t = XL_COLOR2; }
    else if (StartsWith(buf, "Color:")) { rgb = buf + 6; }
    else rgb = buf;                         // original V1 form: "r, g, b"
    if (ValidColorText(rgb) && ParseRgb(rgb, &out->a, &out->b, &out->c)) {
        out->type = t;
        return true;
    }
    return false;
}

const char* XlEffectName(int index) {
    if (index < 0 || index >= XL_EFFECT_COUNT) return "Unknown";
    return kEffectNames[index];
}

// ---------------------------------------------------------------------------
bool SentryParse(const char* line, SentryEvent* out) {
    static const char kPrefix[] = "[Sentry] ";
    memset(out, 0, sizeof(*out));
    line = SkipSpace(line);
    if (!StartsWith(line, kPrefix)) return false;
    char body[64];
    TrimCopy(line + sizeof(kPrefix) - 1, body, sizeof(body));

    static const char* const kTemps[4] = { "CPU:", "GPU:", "eDRAM:", "Mobo:" };
    for (int i = 0; i < 4; ++i) {
        if (StartsWith(body, kTemps[i])) {
            out->type = SN_TEMP;
            out->index = i;
            out->value = (float)atof(body + strlen(kTemps[i]));
            return true;
        }
    }
    if (StartsWith(body, "TitleID:")) {
        out->type = SN_TITLEID;
        TrimCopy(body + 8, out->text, sizeof(out->text));
        return true;
    }
    if (StartsWith(body, "IP:")) {
        out->type = SN_IP;
        TrimCopy(body + 3, out->text, sizeof(out->text));
        return true;
    }
    if (StartsWith(body, "UART Mode") || StartsWith(body, "$mode=2")) { out->type = SN_MODE_UART; return true; }
    if (StartsWith(body, "Default Mode") || StartsWith(body, "$mode=1")) { out->type = SN_MODE_DEFAULT; return true; }
    if (StartsWith(body, "Sentry started")) { out->type = SN_STARTED; return true; }
    out->type = SN_OTHER;
    TrimCopy(body, out->text, sizeof(out->text));
    return true;
}

bool SentryIsTempLine(const char* line) {
    return strstr(line, "eDRAM:") || strstr(line, "GPU:") || strstr(line, "CPU:") || strstr(line, "Mobo:");
}

} // namespace xls
