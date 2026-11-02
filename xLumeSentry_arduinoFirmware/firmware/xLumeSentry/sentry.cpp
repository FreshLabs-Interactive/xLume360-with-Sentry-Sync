#include <Arduino.h>
#include <SD.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"
#include "protocol.h"
#include "sentry.h"
#include "display.h"

namespace {

SentryState g_state;
struct_message g_msg;
bool g_espNow = false;
uint32_t g_lastEspNow = 0;
bool g_espNowDirty = false;

const int kWebLines = 200;
String g_web[kWebLines];
int g_webCount = 0;

void CopyStr(char* dst, size_t size, const char* src) {
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

void SendEspNow() {
#if ESPNOW_ENABLED
    if (!g_espNow) return;
    g_msg.cTemp = g_state.temps[0];
    g_msg.gTemp = g_state.temps[1];
    g_msg.eTemp = g_state.temps[2];
    g_msg.mTemp = g_state.temps[3];
    g_msg.sd = g_state.sdOk;
    g_msg.uart = g_state.uartMode;
    esp_now_send(RECEIVER_MAC, (const uint8_t*)&g_msg, sizeof(g_msg));
#endif
}

// gamelist.txt lines look like: 4d5307e6*Halo 3*Bungie
bool LookUpTitle(const char* titleId, char* game, size_t gameSize, char* dev, size_t devSize) {
    if (!g_state.sdOk) return false;
    File f = SD.open("/gamelist.txt");
    if (!f) return false;
    bool found = false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int a = line.indexOf('*');
        if (a <= 0) continue;
        if (!line.substring(0, a).equalsIgnoreCase(titleId)) continue;
        int b = line.indexOf('*', a + 1);
        String g = b > a ? line.substring(a + 1, b) : line.substring(a + 1);
        String d = b > a ? line.substring(b + 1) : String("");
        CopyStr(game, gameSize, g.c_str());
        CopyStr(dev, devSize, d.c_str());
        found = true;
        break;
    }
    f.close();
    return found;
}

void OnTitle(const char* id, uint32_t now) {
    if (!strcmp(id, "00000000")) return;   // reported during title changes; SentrySync ignored it too
    char upper[12];
    CopyStr(upper, sizeof(upper), id);
    for (char* p = upper; *p; ++p) *p = toupper(*p);
    CopyStr(g_state.titleId, sizeof(g_state.titleId), upper);

    // Like SentrySync: don't pop up the dashboard during the first 25 s of boot.
    if (now < 25000 && !strcmp(upper, "FFFE07D1")) return;

    char game[32] = "", dev[32] = "";
    bool known = LookUpTitle(upper, game, sizeof(game), dev, sizeof(dev));
    if (!known && !strcmp(upper, "584C4D45")) {   // the xLume 360 app itself
        CopyStr(game, sizeof(game), "xLume 360");
        CopyStr(dev, sizeof(dev), "FreshLabs Interactive");
        known = true;
    }
    if (!known) {
        Serial.printf("Title ID %s not in gamelist.txt\n", upper);
        return;
    }
    CopyStr(g_state.gameTitle, sizeof(g_state.gameTitle), game);
    CopyStr(g_state.developer, sizeof(g_state.developer), dev);
    CopyStr(g_msg.gameTitle, sizeof(g_msg.gameTitle), game);
    CopyStr(g_msg.developer, sizeof(g_msg.developer), dev);
    CopyStr(g_msg.tTitleID, sizeof(g_msg.tTitleID), upper);
    g_msg.tidSent = true;
    if (!g_state.uartMode) DisplayShowNowPlaying(game, upper, dev);
}

} // namespace

void SentryBegin(bool sdOk, bool espNowOk) {
    memset(&g_state, 0, sizeof(g_state));
    memset(&g_msg, 0, sizeof(g_msg));
    g_state.sdOk = sdOk;
    g_espNow = espNowOk;
    g_msg.sd = sdOk;
}

const SentryState& SentryGet() { return g_state; }

void SentrySetUartMode(bool uart) {
    if (g_state.uartMode == uart) return;
    g_state.uartMode = uart;
    g_msg.uart = uart;
    DisplaySetUartMode(uart);
    g_espNowDirty = true;
}

void SentryHandleLine(const char* line, uint32_t now) {
    g_state.lastLineMs = now;
    WebLogAdd(line);
    Serial.println(line);   // mirror to the USB serial monitor

    xls::SentryEvent ev;
    bool isSentry = xls::SentryParse(line, &ev);
    if (isSentry) {
        switch (ev.type) {
        case xls::SN_MODE_UART:    SentrySetUartMode(true); break;
        case xls::SN_MODE_DEFAULT: SentrySetUartMode(false); break;
        case xls::SN_TEMP:
            if (!g_state.uartMode) {   // SentrySync only tracked temps in default mode
                g_state.temps[ev.index] = ev.value;
                g_state.haveTemps = true;
            }
            break;
        case xls::SN_TITLEID:
            if (!g_state.uartMode) OnTitle(ev.text, now);
            break;
        case xls::SN_IP:
            if (!g_state.uartMode && strcmp(ev.text, "0.0.0.0") != 0) {
                CopyStr(g_state.xboxIp, sizeof(g_state.xboxIp), ev.text);
                CopyStr(g_msg.xboxipAdd, sizeof(g_msg.xboxipAdd), ev.text);
                DisplayShowXboxIp(ev.text);
            }
            break;
        default:
            break;
        }
        g_espNowDirty = true;
    }
    if (g_state.uartMode && !xls::SentryIsTempLine(line)) DisplayUartLine(line);
}

void SentryLoop(uint32_t now) {
    // SentrySync sent an ESP-NOW packet after every line; do the same, but at
    // most 10 times a second, plus a refresh every 2 s.
    if ((g_espNowDirty && now - g_lastEspNow >= 100) || now - g_lastEspNow >= 2000) {
        g_lastEspNow = now;
        g_espNowDirty = false;
        SendEspNow();
    }
}

// ----------------------------------------------------------------- web log --
void WebLogAdd(const char* line) {
    if (g_webCount >= kWebLines) {
        for (int i = 1; i < kWebLines; ++i) g_web[i - 1] = g_web[i];
        g_webCount = kWebLines - 1;
    }
    g_web[g_webCount++] = line;
}

String WebLogTake() {
    String out;
    for (int i = 0; i < g_webCount; ++i) {
        out += g_web[i];
        out += '\n';
        g_web[i] = String();
    }
    g_webCount = 0;
    return out;
}

void WebLogClear() {
    for (int i = 0; i < g_webCount; ++i) g_web[i] = String();
    g_webCount = 0;
}
