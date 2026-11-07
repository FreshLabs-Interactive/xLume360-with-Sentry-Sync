#include <Arduino.h>
#include <LittleFS.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"
#include "protocol.h"
#include "sentry.h"
#include "display.h"
#include "gamedb.h"

namespace {

SentryState g_state;
struct_message g_msg;
bool g_espNow = false;
uint32_t g_lastEspNow = 0;
bool g_espNowDirty = false;
gamedb::Custom g_custom;
bool g_fsOk = false;
const char* const kCustomFile = "/custom.txt";

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
    g_msg.sd = true;
    g_msg.uart = g_state.uartMode;
    esp_now_send(RECEIVER_MAC, (const uint8_t*)&g_msg, sizeof(g_msg));
#endif
}

bool LookUpTitle(const char* titleId, char* game, size_t gameSize, char* dev, size_t devSize) {
    uint32_t id;
    if (!gamedb::ParseTitleId(titleId, &id)) return false;
    std::string n, d;
    if (gamedb::Lookup(g_custom, id, &n, &d) == gamedb::NONE) {
        if (id != 0x584C4D45) return false;        // the xLume 360 app itself
        n = "xLume 360";
        d = "FreshLabs Interactive";
    }
    CopyStr(game, gameSize, n.c_str());
    CopyStr(dev, devSize, d.c_str());
    return true;
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
    g_state.titleKnown = known;
    if (!known) {
        // Not in the list: clear the old name so the web page doesn't show the previous game,
        // and offer the ID there for adding. No pop-up, as before.
        Serial.printf("Title ID %s not in the game list (add it on the web page)\n", upper);
        g_state.gameTitle[0] = g_state.developer[0] = '\0';
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

void SentryBegin(bool espNowOk) {
    memset(&g_state, 0, sizeof(g_state));
    memset(&g_msg, 0, sizeof(g_msg));
    g_espNow = espNowOk;
    g_msg.sd = true;

    // Custom titles live in the flash file system (the partition SPIFFS used to have).
    // begin(true) formats it the first time, which is fine: it only holds this file.
    g_fsOk = LittleFS.begin(true);
    if (g_fsOk) {
        File f = LittleFS.open(kCustomFile, "r");
        if (f) {
            std::string text;
            text.reserve(f.size());
            while (f.available()) {
                char buf[256];
                int n = f.read((uint8_t*)buf, sizeof(buf));
                if (n <= 0) break;
                text.append(buf, n);
            }
            f.close();
            g_custom.Load(text);
        }
    }
    Serial.printf("Game list: %u built in, %u custom%s\n", (unsigned)gamedb::BuiltinCount(),
                  (unsigned)g_custom.All().size(), g_fsOk ? "" : " (flash file system not available)");
}

gamedb::Custom& SentryCustomTitles() { return g_custom; }
bool SentryStorageOk() { return g_fsOk; }

bool SentrySaveCustomTitles() {
    bool ok = false;
    if (g_fsOk) {
        std::string text = g_custom.Save();
        File f = LittleFS.open("/custom.tmp", "w");
        if (f) {
            ok = f.write((const uint8_t*)text.data(), text.size()) == text.size();
            f.close();
            // Write to a temp file, then swap, so a power cut can't leave half a list.
            if (ok) ok = LittleFS.remove(kCustomFile) || !LittleFS.exists(kCustomFile);
            if (ok) ok = LittleFS.rename("/custom.tmp", kCustomFile);
        }
    }
    // If the game that's running was just added or renamed, show the new name.
    if (g_state.titleId[0]) {
        char game[32] = "", dev[32] = "";
        bool known = LookUpTitle(g_state.titleId, game, sizeof(game), dev, sizeof(dev));
        g_state.titleKnown = known;
        CopyStr(g_state.gameTitle, sizeof(g_state.gameTitle), game);
        CopyStr(g_state.developer, sizeof(g_state.developer), dev);
        if (known) {
            CopyStr(g_msg.gameTitle, sizeof(g_msg.gameTitle), game);
            CopyStr(g_msg.developer, sizeof(g_msg.developer), dev);
            g_espNowDirty = true;
        }
    }
    return ok;
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
