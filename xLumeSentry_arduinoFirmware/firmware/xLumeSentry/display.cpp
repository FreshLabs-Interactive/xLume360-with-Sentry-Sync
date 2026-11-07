#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "protocol.h"
#include "display.h"
#include "led_engine.h"
#include "sentry.h"
#include "JFresh_AI_128x64.h"

namespace {

TFT_eSPI tft = TFT_eSPI();

// Colours given as normal RGB; swapped for Sentry's blue-green-red panel.
uint16_t C(uint8_t r, uint8_t g, uint8_t b) {
    return TFT_SWAP_RED_BLUE ? tft.color565(b, g, r) : tft.color565(r, g, b);
}

enum Page { PAGE_NONE, PAGE_BOOT, PAGE_XBOXIP, PAGE_NOWPLAYING, PAGE_OTA };

struct Overlay {
    Page page;
    uint32_t durationMs;
    char a[48], b[48], c[48], d[48];
};

const int kQueue = 6;
Overlay g_queue[kQueue];
int g_queueLen = 0;
Overlay g_current;
bool g_overlayActive = false;
uint32_t g_overlayStart = 0;

bool g_uart = false;
bool g_screenDirty = true;       // base screen (status/UART) needs a full redraw
char g_webAddr[40] = "";

// status screen caches
float g_shownTemps[4] = { -999, -999, -999, -999 };
bool g_shownHaveTemps = false;
uint32_t g_lastTempDraw = 0;
uint32_t g_shownLedCounter = 0xFFFFFFFF;
uint32_t g_lastDots = 0;

// UART screen
const int kUartLines = 8;
const int kUartCols = 26;          // text size 2 = 12 px per char
String g_uartLines[kUartLines];
int g_uartCount = 0;
bool g_uartDirty = false;
uint32_t g_lastUartDraw = 0;

// Panel geometry (bottom-left, clear of the SentrySync logo)
const int PX = 4, PY = 122, PW = 148, PH = 114;

void CopyStr(char* dst, size_t size, const char* src) {
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

// Redraws part of the SentrySync background image (row by row with
// pushImage: ~50x faster than SentrySync's drawPixel loop).
void DrawBackgroundRect(int x0, int y0, int w, int h) {
    static uint16_t row[JFRESH_AI_128X64_WIDTH];
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > JFRESH_AI_128X64_WIDTH) w = JFRESH_AI_128X64_WIDTH - x0;
    if (y0 + h > JFRESH_AI_128X64_HEIGHT) h = JFRESH_AI_128X64_HEIGHT - y0;
    if (w <= 0 || h <= 0) return;
    tft.setSwapBytes(true);
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint16_t c = pgm_read_word(&JFresh_AI_128x64[y * JFRESH_AI_128X64_WIDTH + x0 + x]);
            if (TFT_SWAP_RED_BLUE) {
                uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
                c = (b << 11) | (g << 5) | r;
            }
            row[x] = c;
        }
        tft.pushImage(x0, y, w, 1, row);
    }
    tft.setSwapBytes(false);
}

void DrawBackground() { DrawBackgroundRect(0, 0, JFRESH_AI_128X64_WIDTH, JFRESH_AI_128X64_HEIGHT); }

// ------------------------------------------------------------- status page --
void DrawTemps(bool force) {
    const SentryState& s = SentryGet();
    bool changed = force || s.haveTemps != g_shownHaveTemps;
    for (int i = 0; i < 4 && !changed; ++i) if (s.temps[i] != g_shownTemps[i]) changed = true;
    if (!changed) return;
    // Same order, size and colour as SentrySync: GPU, CPU, EDR, MB in red.
    static const char* const kLabels[4] = { "GPU: ", "CPU: ", "EDR: ", " MB: " };
    static const int kIndex[4] = { 1, 0, 2, 3 };   // sentry order -> our temps[] order
    tft.setTextSize(3);
    tft.setTextColor(C(255, 0, 0));               // transparent text...
    for (int i = 0; i < 4; ++i) {
        char line[24];
        if (s.haveTemps) snprintf(line, sizeof(line), "%s%.1fC", kLabels[i], s.temps[kIndex[i]]);
        else snprintf(line, sizeof(line), "%s--.-C", kLabels[i]);
        // ...over freshly restored background, so old digits vanish and the
        // logo next to the text is never cut into.
        DrawBackgroundRect(0, 15 + i * 24, 18 * 12, 24);
        tft.setCursor(0, 15 + i * 24);
        tft.print(line);
    }
    for (int i = 0; i < 4; ++i) g_shownTemps[i] = s.temps[i];
    g_shownHaveTemps = s.haveTemps;
}

void DrawPanelFrame() {
    tft.fillRoundRect(PX, PY, PW, PH, 8, C(42, 52, 66));
    tft.fillRoundRect(PX + 1, PY + 1, PW - 2, PH - 2, 7, C(18, 23, 31));
}

void DrawPanelText(const LedStatus& st) {
    const uint16_t bg = C(18, 23, 31);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    // header
    tft.setTextColor(C(94, 214, 201), bg);
    tft.drawString("LIGHTING", PX + 8, PY + 6, 1);
    char buf[48];
    snprintf(buf, sizeof(buf), "%3u LEDs", st.count);
    tft.setTextColor(C(138, 150, 166), bg);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, PX + PW - 8, PY + 6, 1);
    tft.setTextDatum(TL_DATUM);
    // effect name (font 2 = 16 px)
    tft.fillRect(PX + 6, PY + 18, PW - 12, 17, bg);
    tft.setTextColor(TFT_WHITE, bg);
    tft.drawString(xls::XlEffectName(st.effect), PX + 8, PY + 19, 2);
    // colour chips + brightness
    tft.fillRoundRect(PX + 8, PY + 40, 22, 14, 3, C(st.c1[0], st.c1[1], st.c1[2]));
    tft.fillRoundRect(PX + 34, PY + 40, 22, 14, 3, C(st.c2[0], st.c2[1], st.c2[2]));
    tft.fillRect(PX + 60, PY + 38, PW - 66, 17, bg);
    snprintf(buf, sizeof(buf), "%d%%", st.brightness * 100 / 255);
    tft.setTextColor(TFT_WHITE, bg);
    tft.drawString(buf, PX + 62, PY + 39, 2);
    snprintf(buf, sizeof(buf), "spd %u", st.speed);
    tft.setTextColor(C(138, 150, 166), bg);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, PX + PW - 8, PY + 43, 1);
    tft.setTextDatum(TL_DATUM);
    // where to find the web page
    tft.fillRect(PX + 6, PY + PH - 14, PW - 12, 10, bg);
    tft.setTextColor(C(138, 150, 166), bg);
    tft.drawString(g_webAddr, PX + 8, PY + PH - 13, 1);
}

void DrawPanelDots(const LedStatus& st) {
    const int y = PY + 76, r = 4;
    const int x0 = PX + 12, step = (PW - 24) / (LED_PREVIEW_DOTS - 1);
    for (int i = 0; i < LED_PREVIEW_DOTS; ++i) {
        uint8_t rr = st.preview[i][0], gg = st.preview[i][1], bb = st.preview[i][2];
        // Show roughly what the strip shows, but never so dark it vanishes.
        uint16_t scale = st.brightness < 70 ? 70 : st.brightness;
        rr = rr * scale / 255; gg = gg * scale / 255; bb = bb * scale / 255;
        tft.fillCircle(x0 + i * step, y, r + 1, C(40, 46, 56));
        tft.fillCircle(x0 + i * step, y, r, C(rr, gg, bb));
    }
}

void DrawStatusBase() {
    DrawBackground();
    DrawTemps(true);
    DrawPanelFrame();
    g_shownLedCounter = 0xFFFFFFFF;
}

// --------------------------------------------------------------- UART page --
void DrawUart(bool full) {
    if (full) tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextWrap(false);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < kUartLines; ++i) {
        String s = i < g_uartCount ? g_uartLines[i] : String("");
        if ((int)s.length() > kUartCols) s = s.substring(0, kUartCols);
        while ((int)s.length() < kUartCols) s += ' ';
        tft.setCursor(5, 10 + i * 28);
        tft.print(s);
    }
}

// --------------------------------------------------------------- overlays --
void DrawOverlay(const Overlay& o, uint32_t elapsed) {
    switch (o.page) {
    case PAGE_BOOT: {
        if (elapsed == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            const char* lines[4] = { o.a, o.b, o.c, o.d };
            for (int i = 0; i < 4; ++i) {
                tft.setCursor(10, 40 + i * 36);
                tft.print(lines[i]);
            }
        }
        break;
    }
    case PAGE_XBOXIP:
        if (elapsed == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextSize(2);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setCursor(20, 25);
            tft.println("Xbox IP Address:");
            tft.setCursor(20, 75);
            tft.println(o.a);
        }
        break;
    case PAGE_NOWPLAYING:
        if (elapsed == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(3);
            tft.setCursor(50, 25);
            tft.println("Now Playing... ");
            tft.setTextSize(2);
            tft.setCursor(20, 60);
            tft.println(o.a);
            tft.setCursor(20, 110);
            tft.println("Title ID: ");
            tft.setCursor(20, 130);
            tft.println(o.b);
            tft.setCursor(20, 180);
            tft.println("Developer:");
            tft.setCursor(20, 200);
            tft.println(o.c);
        }
        break;
    default:
        break;
    }
}

void Enqueue(Page page, uint32_t ms, const char* a, const char* b, const char* c, const char* d) {
    // A newer Xbox IP / Now Playing replaces one that's still waiting.
    for (int i = 0; i < g_queueLen; ++i) {
        if (g_queue[i].page == page && (page == PAGE_XBOXIP || page == PAGE_NOWPLAYING)) {
            for (int k = i + 1; k < g_queueLen; ++k) g_queue[k - 1] = g_queue[k];
            --g_queueLen;
            break;
        }
    }
    if (g_queueLen >= kQueue) return;
    Overlay& o = g_queue[g_queueLen++];
    o.page = page;
    o.durationMs = ms;
    CopyStr(o.a, sizeof(o.a), a);
    CopyStr(o.b, sizeof(o.b), b);
    CopyStr(o.c, sizeof(o.c), c);
    CopyStr(o.d, sizeof(o.d), d);
}

} // namespace

// =============================================================================
void DisplayBegin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.print("xLume + Sentry");
    tft.setCursor(10, 80);
    tft.print("Starting...");
}

void DisplayShowBoot(const char* l1, const char* l2, const char* l3, const char* l4) {
    Enqueue(PAGE_BOOT, 5000, l1, l2, l3, l4);
}
void DisplayShowXboxIp(const char* ip) { Enqueue(PAGE_XBOXIP, 5000, ip, "", "", ""); }
void DisplayShowNowPlaying(const char* game, const char* id, const char* dev) {
    Enqueue(PAGE_NOWPLAYING, 8000, game, id, dev, "");
}

void DisplaySetWebAddress(const char* text) {
    CopyStr(g_webAddr, sizeof(g_webAddr), text);
    g_shownLedCounter = 0xFFFFFFFF;   // redraw panel text
}

void DisplaySetUartMode(bool uart) {
    if (g_uart == uart) return;
    g_uart = uart;
    g_uartCount = 0;
    g_screenDirty = true;
}

void DisplayUartLine(const char* line) {
    if (g_uartCount >= kUartLines) {
        for (int i = 1; i < kUartLines; ++i) g_uartLines[i - 1] = g_uartLines[i];
        g_uartCount = kUartLines - 1;
    }
    g_uartLines[g_uartCount++] = line;
    g_uartDirty = true;
}

void DisplayUpdate(uint32_t now) {
    // ---- overlays first
    if (g_overlayActive) {
        uint32_t elapsed = now - g_overlayStart;
        if (elapsed >= g_current.durationMs) {
            g_overlayActive = false;
            g_screenDirty = true;
        } else {
            return;
        }
    }
    if (!g_overlayActive && g_queueLen > 0) {
        g_current = g_queue[0];
        for (int k = 1; k < g_queueLen; ++k) g_queue[k - 1] = g_queue[k];
        --g_queueLen;
        g_overlayActive = true;
        g_overlayStart = now;
        DrawOverlay(g_current, 0);
        return;
    }

    // ---- base screen
    if (g_uart) {
        if (g_screenDirty) { DrawUart(true); g_screenDirty = false; g_uartDirty = false; g_lastUartDraw = now; }
        else if (g_uartDirty && now - g_lastUartDraw >= 100) { DrawUart(false); g_uartDirty = false; g_lastUartDraw = now; }
        return;
    }

    if (g_screenDirty) {
        DrawStatusBase();
        g_screenDirty = false;
        g_lastTempDraw = now;
    }
    if (now - g_lastTempDraw >= 2000) {   // SentrySync refreshed temps every 2 s
        g_lastTempDraw = now;
        DrawTemps(false);
    }
    if (now - g_lastDots >= 120) {
        g_lastDots = now;
        LedStatus st;
        LedGetStatus(&st);
        if (st.changeCounter != g_shownLedCounter) {
            g_shownLedCounter = st.changeCounter;
            DrawPanelText(st);
        }
        DrawPanelDots(st);
    }
}

void DisplayOtaProgress(int percent) {
    static int shown = -1;
    if (percent == 0 || shown < 0) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(20, 60);
        tft.print("Updating firmware");
        tft.setCursor(20, 90);
        tft.print("Don't power off");
        tft.drawRoundRect(20, 140, 280, 24, 6, C(94, 214, 201));
        shown = 0;
    }
    if (percent != shown) {
        shown = percent;
        tft.fillRoundRect(23, 143, 274 * percent / 100, 18, 4, C(94, 214, 201));
        tft.setCursor(130, 180);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        char b[8];
        snprintf(b, sizeof(b), "%3d%%", percent);
        tft.print(b);
    }
    if (percent >= 100) shown = -1;
}

void DisplayOtaDone(bool ok, const char* message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ok ? C(63, 185, 80) : C(248, 81, 73), TFT_BLACK);
    tft.setCursor(20, 90);
    tft.print(ok ? "Update complete" : "Update failed");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.print(message ? message : "");
    g_screenDirty = true;
}
