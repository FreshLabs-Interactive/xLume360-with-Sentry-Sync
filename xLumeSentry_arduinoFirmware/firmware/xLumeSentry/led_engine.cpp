#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include "config.h"
#include "protocol.h"
#include "led_engine.h"

namespace {

struct Look {
    uint8_t effect;
    uint8_t brightness;
    uint8_t speed;
    uint16_t count;
    CRGB c1;
    CRGB c2;
};

const uint32_t kSaveMagic = 0x584C5331;   // 'XLS1'
const uint32_t kSaveDelayMs = 3000;       // same debounce as the original firmware

CRGB g_leds[LED_MAX_COUNT];
CLEDController* g_ctrl = nullptr;
Look g_look;
Look g_saved;
bool g_dirty = false;
uint32_t g_lastChange = 0;
uint32_t g_changeCounter = 0;

QueueHandle_t g_queue = nullptr;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
LedStatus g_status;

// Effect state
uint32_t g_animMs = 0;           // animation clock, scaled by speed
uint8_t g_heat[LED_MAX_COUNT];   // Fire
uint32_t g_effectStart = 0;

// ---------------------------------------------------------------- storage ----
void LoadLook() {
    g_look.effect = 0;
    g_look.brightness = 160;
    g_look.speed = 128;
    g_look.count = 30;
    g_look.c1 = CRGB(0, 200, 180);
    g_look.c2 = CRGB(120, 0, 255);
    Preferences p;
    if (p.begin("xlume", true)) {
        if (p.getUInt("magic", 0) == kSaveMagic) {
            g_look.effect = p.getUChar("effect", g_look.effect);
            g_look.brightness = p.getUChar("bright", g_look.brightness);
            g_look.speed = p.getUChar("speed", g_look.speed);
            g_look.count = p.getUShort("count", g_look.count);
            uint32_t c1 = p.getUInt("c1", 0x00C8B4);
            uint32_t c2 = p.getUInt("c2", 0x7800FF);
            g_look.c1 = CRGB((c1 >> 16) & 255, (c1 >> 8) & 255, c1 & 255);
            g_look.c2 = CRGB((c2 >> 16) & 255, (c2 >> 8) & 255, c2 & 255);
        }
        p.end();
    }
    if (g_look.effect >= xls::XL_EFFECT_COUNT) g_look.effect = 0;
    if (g_look.speed < 1) g_look.speed = 1;
    if (g_look.count < 1 || g_look.count > LED_MAX_COUNT) g_look.count = 30;
    g_saved = g_look;
}

bool SameLook(const Look& a, const Look& b) {
    return a.effect == b.effect && a.brightness == b.brightness && a.speed == b.speed &&
           a.count == b.count && a.c1 == b.c1 && a.c2 == b.c2;
}

void SaveLookIfNeeded(uint32_t now) {
    if (!g_dirty || now - g_lastChange < kSaveDelayMs) return;
    g_dirty = false;
    if (SameLook(g_look, g_saved)) return;   // e.g. a short flash that was undone
    Preferences p;
    if (!p.begin("xlume", false)) return;
    p.putUInt("magic", kSaveMagic);
    p.putUChar("effect", g_look.effect);
    p.putUChar("bright", g_look.brightness);
    p.putUChar("speed", g_look.speed);
    p.putUShort("count", g_look.count);
    p.putUInt("c1", ((uint32_t)g_look.c1.r << 16) | ((uint32_t)g_look.c1.g << 8) | g_look.c1.b);
    p.putUInt("c2", ((uint32_t)g_look.c2.r << 16) | ((uint32_t)g_look.c2.g << 8) | g_look.c2.b);
    p.end();
    g_saved = g_look;
}

// ------------------------------------------------------------- commands ----
void SetCount(uint16_t n) {
    if (n == g_look.count) return;
    // Blank the whole strip first so LEDs past the new end go dark.
    fill_solid(g_leds, LED_MAX_COUNT, CRGB::Black);
    g_ctrl->setLeds(g_leds, LED_MAX_COUNT);
    FastLED.show();
    g_look.count = n;
    g_ctrl->setLeds(g_leds, n);
}

void Apply(const xls::XlCommand& c) {
    switch (c.type) {
    case xls::XL_COLOR:      g_look.c1 = CRGB(c.a, c.b, c.c); break;
    case xls::XL_COLOR2:     g_look.c2 = CRGB(c.a, c.b, c.c); break;
    case xls::XL_EFFECT:
        if (g_look.effect != c.a) {
            g_look.effect = (uint8_t)c.a;
            g_effectStart = g_animMs;
            memset(g_heat, 0, sizeof(g_heat));
        }
        break;
    case xls::XL_BRIGHTNESS: g_look.brightness = (uint8_t)c.a; break;
    case xls::XL_SPEED:      g_look.speed = (uint8_t)c.a; break;
    case xls::XL_LEDCOUNT:   SetCount((uint16_t)c.a); break;
    default: return;
    }
    g_dirty = true;
    g_lastChange = millis();
    ++g_changeCounter;
}

// -------------------------------------------------------------- effects ----
// Recreations of the xLume V2 effects (the original firmware is closed source,
// so these are look-alikes built from the effect names, not byte-exact).
uint8_t Rand8(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x632BE5ABu) * 0x85EBCA6Bu;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    return (uint8_t)(x >> 8);
}

void Render(uint32_t dtMs) {
    const int n = g_look.count;
    const CRGB c1 = g_look.c1, c2 = g_look.c2;
    // speed 128 = normal, 255 = ~2x, 1 = nearly still
    g_animMs += dtMs * g_look.speed / 128;
    const uint32_t t = g_animMs;

    switch (g_look.effect) {
    case 0:  // Static Color
        fill_solid(g_leds, n, c1);
        break;
    case 1: {  // Static Gradient: colour into a hue-shifted partner
        CHSV h = rgb2hsv_approximate(c1);
        CRGB partner = CHSV(h.hue + 42, h.sat, 255);
        partner.nscale8_video(max(c1.r, max(c1.g, c1.b)));
        for (int i = 0; i < n; ++i) g_leds[i] = blend(c1, partner, n > 1 ? i * 255 / (n - 1) : 0);
        break;
    }
    case 2: {  // Static Pulse
        uint8_t k = 40 + scale8(sin8(t / 8), 215);
        CRGB c = c1; c.nscale8_video(k);
        fill_solid(g_leds, n, c);
        break;
    }
    case 3: {  // Static Twinkle: dim base, pixels sparkle up in the colour
        for (int i = 0; i < n; ++i) {
            uint32_t phase = t / 3 + Rand8(i, 7) * 4;
            uint32_t slot = phase / 256;
            uint8_t ph = phase & 255;
            CRGB c = c1;
            if (Rand8(i, slot) > 150) c.nscale8_video(30 + scale8(sin8(ph / 2), 225));
            else c.nscale8_video(30);
            g_leds[i] = c;
        }
        break;
    }
    case 4:  // LEDs Off
        fill_solid(g_leds, n, CRGB::Black);
        break;
    case 5:  // Color Wave
        for (int i = 0; i < n; ++i) g_leds[i] = CHSV((uint8_t)(i * 256 / max(1, n) - t / 12), 255, 255);
        break;
    case 6:  // Color Cycle
        fill_solid(g_leds, n, CHSV(t / 40, 255, 255));
        break;
    case 7: {  // Dynamic Pulse
        uint8_t k = 40 + scale8(sin8(t / 8), 215);
        fill_solid(g_leds, n, CHSV(t / 60, 255, k));
        break;
    }
    case 8: {  // Dynamic Twinkle: random colours appear and fade
        fadeToBlackBy(g_leds, n, max(1, (int)(dtMs * g_look.speed / 160)));
        if (random8() < 30 + g_look.speed / 4) g_leds[random16(n)] = CHSV(random8(), 220, 255);
        break;
    }
    case 9: {  // Fire (Fire2012)
        const uint8_t cooling = 55, sparking = 120;
        static uint32_t acc = 0;
        acc += dtMs * g_look.speed / 128;
        if (acc < 15) break;
        acc = 0;
        for (int i = 0; i < n; ++i) g_heat[i] = qsub8(g_heat[i], random8(0, ((cooling * 10) / max(1, n)) + 2));
        for (int k = n - 1; k >= 2; --k) g_heat[k] = (g_heat[k - 1] + g_heat[k - 2] + g_heat[k - 2]) / 3;
        if (random8() < sparking) { int y = random8(min(7, n)); g_heat[y] = qadd8(g_heat[y], random8(160, 255)); }
        for (int j = 0; j < n; ++j) g_leds[j] = HeatColor(g_heat[j]);
        break;
    }
    case 10: {  // Confetti
        fadeToBlackBy(g_leds, n, 10);
        if (random8() < 80 + g_look.speed / 3) g_leds[random16(n)] += CHSV(t / 30 + random8(64), 200, 255);
        break;
    }
    case 11: {  // Sparkle: base colour with white glints
        CRGB base = c1; base.nscale8_video(70);
        fill_solid(g_leds, n, base);
        for (int k = 0; k < 1 + n / 25; ++k)
            if (random8() < 40 + g_look.speed / 3) g_leds[random16(n)] = CRGB::White;
        break;
    }
    case 12: {  // Theater Chase
        int step = t / 90;
        for (int i = 0; i < n; ++i) g_leds[i] = ((i + step) % 3 == 0) ? c1 : CRGB::Black;
        break;
    }
    case 13: {  // Rainbow Chase
        int step = t / 90;
        for (int i = 0; i < n; ++i)
            g_leds[i] = ((i + step) % 3 == 0) ? CRGB(CHSV(i * 256 / max(1, n) + t / 20, 255, 255)) : CRGB::Black;
        break;
    }
    case 14: {  // Larson Scanner
        fadeToBlackBy(g_leds, n, 60);
        uint16_t wave = (uint16_t)(sin16((uint16_t)(t * 6)) + 32768);   // 0..65535, back and forth
        int pos = n > 1 ? scale16(wave, n - 1) : 0;
        g_leds[pos] = c1;
        if (pos > 0) g_leds[pos - 1] |= CRGB(c1).nscale8_video(120);
        if (pos < n - 1) g_leds[pos + 1] |= CRGB(c1).nscale8_video(120);
        break;
    }
    case 15:    // Comet
    case 16: {  // Meteor Rain
        bool meteor = g_look.effect == 16;
        for (int i = 0; i < n; ++i) {
            if (!meteor) g_leds[i].fadeToBlackBy(48);
            else if (random8() > 110) g_leds[i].fadeToBlackBy(64);
        }
        int head = (t / 30) % (n + (meteor ? n / 3 : 0) + 1);
        for (int k = 0; k < (meteor ? 3 : 2); ++k) {
            int p = head - k;
            if (p >= 0 && p < n) g_leds[p] = (k == 0) ? CRGB(c1) + CRGB(40, 40, 40) : c1;
        }
        break;
    }
    case 17: {  // Color Wipe: fill, then wipe out
        uint32_t cycle = (uint32_t)n * 2 * 40;
        uint32_t p = (t - g_effectStart) % max<uint32_t>(1, cycle);
        int edge = p / 40;
        for (int i = 0; i < n; ++i) {
            bool on = edge < n ? (i <= edge) : (i > edge - n);
            g_leds[i] = on ? c1 : CRGB::Black;
        }
        break;
    }
    case 18:  // Two-Color Gradient: colour 1 at the ends, colour 2 in the middle
        for (int i = 0; i < n; ++i) {
            int f = n > 1 ? 255 - abs(i * 510 / (n - 1) - 255) : 0;
            g_leds[i] = blend(c1, c2, (uint8_t)f);
        }
        break;
    case 19: {  // Two-Color Chase
        int step = t / 120;
        for (int i = 0; i < n; ++i) g_leds[i] = (((i + step) / 3) % 2 == 0) ? c1 : c2;
        break;
    }
    case 20:  // Strobe
        fill_solid(g_leds, n, ((t / 25) % 10 == 0) ? c1 : CRGB::Black);
        break;
    case 21: {  // Police Lights: left red / right blue, double flashes
        uint32_t f = t % 1000;
        bool flash = ((f % 250) < 90);
        bool left = f < 500;
        for (int i = 0; i < n; ++i) {
            bool isLeft = i < n / 2;
            if (flash && isLeft == left) g_leds[i] = isLeft ? CRGB::Red : CRGB::Blue;
            else g_leds[i] = CRGB::Black;
        }
        break;
    }
    case 22: {  // Candle Flicker
        static uint32_t acc = 0;
        acc += dtMs;
        if (acc < 50) break;
        acc = 0;
        for (int i = 0; i < n; i += 3) {
            CRGB c = c1;
            c.nscale8_video(150 + random8(105));
            for (int k = i; k < i + 3 && k < n; ++k) g_leds[k] = c;
        }
        break;
    }
    default:
        fill_solid(g_leds, n, c1);
        break;
    }
}

void PublishStatus() {
    LedStatus s;
    s.effect = g_look.effect;
    s.brightness = g_look.brightness;
    s.speed = g_look.speed;
    s.count = g_look.count;
    s.c1[0] = g_look.c1.r; s.c1[1] = g_look.c1.g; s.c1[2] = g_look.c1.b;
    s.c2[0] = g_look.c2.r; s.c2[1] = g_look.c2.g; s.c2[2] = g_look.c2.b;
    for (int i = 0; i < LED_PREVIEW_DOTS; ++i) {
        int idx = g_look.count > 1 ? i * (g_look.count - 1) / (LED_PREVIEW_DOTS - 1) : 0;
        s.preview[i][0] = g_leds[idx].r;
        s.preview[i][1] = g_leds[idx].g;
        s.preview[i][2] = g_leds[idx].b;
    }
    s.changeCounter = g_changeCounter;
    portENTER_CRITICAL(&g_mux);
    g_status = s;
    portEXIT_CRITICAL(&g_mux);
}

void LedTask(void*) {
    uint32_t last = millis();
    char body[96];
    for (;;) {
        // Apply every queued command before drawing the next frame.
        while (xQueueReceive(g_queue, body, 0) == pdTRUE) {
            xls::XlCommand c;
            if (xls::XlParse(body, &c)) Apply(c);
        }
        uint32_t now = millis();
        uint32_t dt = now - last;
        last = now;
        if (dt > 100) dt = 100;
        Render(dt);
        FastLED.setBrightness(g_look.brightness);
        FastLED.show();
        PublishStatus();
        SaveLookIfNeeded(now);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

} // namespace

void LedBegin() {
    LoadLook();
    g_ctrl = &FastLED.addLeds<LED_TYPE, LED_DATA_PIN, LED_COLOR_ORDER>(g_leds, LED_MAX_COUNT);
    fill_solid(g_leds, LED_MAX_COUNT, CRGB::Black);
    FastLED.setBrightness(0);
    FastLED.show();                         // blank anything past the saved count
    g_ctrl->setLeds(g_leds, g_look.count);
    if (LED_MAX_MILLIAMPS > 0) FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MILLIAMPS);
    g_queue = xQueueCreate(48, 96);
    PublishStatus();
    // Core 1 (with loop()), higher priority than loop() so screen drawing and
    // web requests never stall the animation. Wi-Fi lives on core 0.
    xTaskCreatePinnedToCore(LedTask, "leds", 4096, nullptr, 2, nullptr, 1);
}

bool LedPost(const char* body) {
    if (!g_queue || !body) return false;
    char buf[96];
    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    return xQueueSend(g_queue, buf, 0) == pdTRUE;
}

void LedGetStatus(LedStatus* out) {
    portENTER_CRITICAL(&g_mux);
    *out = g_status;
    portEXIT_CRITICAL(&g_mux);
}
