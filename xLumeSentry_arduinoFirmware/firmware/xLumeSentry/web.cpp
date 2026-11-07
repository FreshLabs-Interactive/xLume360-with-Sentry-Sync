#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include "config.h"
#include "protocol.h"
#include "led_engine.h"
#include "sentry.h"
#include "display.h"
#include "web.h"
#include "web_page.h"
#include "gamedb.h"

extern const char* const kFirmwareVersion;

namespace {

WebServer server(80);
String g_net;
bool g_otaAuthorized = false;
String g_otaError;

String JsonEscape(const char* s) {
    String out;
    for (; s && *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out += (char)c;
    }
    return out;
}

void HandleRoot() { server.send_P(200, "text/html", kIndexHtml); }

// Same endpoint the SentrySync page used: returns (and clears) new log lines.
void HandleSerialData() { server.send(200, "text/plain", WebLogTake()); }
void HandleClear() { WebLogClear(); server.send(200, "text/plain", "Console cleared"); }
void HandleAutoScroll() { server.send(200, "text/plain", "1"); }

void HandleStatus() {
    const SentryState& s = SentryGet();
    LedStatus l;
    LedGetStatus(&l);
    String j;
    j.reserve(900);
    j += "{\"fw\":\""; j += kFirmwareVersion; j += "\",\"net\":\""; j += JsonEscape(g_net.c_str()); j += "\"";
    if (s.haveTemps) {
        char b[96];
        snprintf(b, sizeof(b), ",\"temps\":[%.1f,%.1f,%.1f,%.1f]", s.temps[0], s.temps[1], s.temps[2], s.temps[3]);
        j += b;
    }
    j += ",\"game\":\""; j += JsonEscape(s.gameTitle); j += "\"";
    j += ",\"dev\":\""; j += JsonEscape(s.developer); j += "\"";
    j += ",\"titleId\":\""; j += JsonEscape(s.titleId); j += "\"";
    j += ",\"xboxIp\":\""; j += JsonEscape(s.xboxIp); j += "\"";
    j += ",\"titleKnown\":"; j += s.titleKnown ? "true" : "false";
    j += ",\"titles\":"; j += String(gamedb::BuiltinCount());
    j += ",\"custom\":"; j += String((unsigned)SentryCustomTitles().All().size());
    j += ",\"storage\":"; j += SentryStorageOk() ? "true" : "false";
    j += ",\"uart\":"; j += s.uartMode ? "true" : "false";
    j += ",\"lastLineAgo\":"; j += s.lastLineMs ? String((millis() - s.lastLineMs) / 1000) : String("-1");
    char b[160];
    snprintf(b, sizeof(b), ",\"led\":{\"effect\":%u,\"bri\":%u,\"spd\":%u,\"count\":%u,\"c1\":[%u,%u,%u],\"c2\":[%u,%u,%u],\"preview\":[",
             l.effect, l.brightness, l.speed, l.count, l.c1[0], l.c1[1], l.c1[2], l.c2[0], l.c2[1], l.c2[2]);
    j += b;
    for (int i = 0; i < LED_PREVIEW_DOTS; ++i) {
        snprintf(b, sizeof(b), "%s[%u,%u,%u]", i ? "," : "", l.preview[i][0], l.preview[i][1], l.preview[i][2]);
        j += b;
    }
    j += "]}}";
    server.send(200, "application/json", j);
}

bool HexToRgb(const String& h, int* r, int* g, int* b) {
    if (h.length() != 6) return false;
    for (unsigned i = 0; i < 6; ++i) if (!isxdigit((unsigned char)h[i])) return false;
    long v = strtol(h.c_str(), nullptr, 16);
    *r = (v >> 16) & 255; *g = (v >> 8) & 255; *b = v & 255;
    return true;
}

// /api/led?effect=9&c1=ff8800&c2=0000ff&bri=200&spd=128&count=30
// Each value becomes a normal xLume command, so the web page and the console
// go through exactly the same code.
void HandleLed() {
    char body[64];
    int r, g, b, sent = 0;
    if (server.hasArg("bri"))   { snprintf(body, sizeof(body), "Brightness: %ld", server.arg("bri").toInt()); sent += LedPost(body); }
    if (server.hasArg("spd"))   { snprintf(body, sizeof(body), "Speed: %ld", server.arg("spd").toInt()); sent += LedPost(body); }
    if (server.hasArg("count")) { snprintf(body, sizeof(body), "ledCount: %ld", server.arg("count").toInt()); sent += LedPost(body); }
    if (server.hasArg("c2") && HexToRgb(server.arg("c2"), &r, &g, &b)) { snprintf(body, sizeof(body), "Color2: %d, %d, %d", r, g, b); sent += LedPost(body); }
    if (server.hasArg("c1") && HexToRgb(server.arg("c1"), &r, &g, &b)) { snprintf(body, sizeof(body), "Color: %d, %d, %d", r, g, b); sent += LedPost(body); }
    if (server.hasArg("effect")) { snprintf(body, sizeof(body), "Effects: %ld", server.arg("effect").toInt()); sent += LedPost(body); }
    server.send(sent ? 200 : 400, "text/plain", sent ? "OK" : "Nothing to do");
}

void HandleMode() {
    SentrySetUartMode(server.arg("uart") == "1");
    server.send(200, "text/plain", "OK");
}

// ---- game list ----------------------------------------------------------------
String TitleJson(uint32_t id, const std::string& name, const std::string& dev, const char* source) {
    char hex[9];
    gamedb::FormatTitleId(id, hex);
    String j = "{\"id\":\"";
    j += hex; j += "\",\"name\":\""; j += JsonEscape(name.c_str());
    j += "\",\"dev\":\""; j += JsonEscape(dev.c_str()); j += "\",\"source\":\""; j += source; j += "\"}";
    return j;
}

bool ArgId(uint32_t* id) {
    if (gamedb::ParseTitleId(server.arg("id").c_str(), id)) return true;
    server.send(400, "text/plain", "Title ID must be 8 hex digits, like 4D5307E6");
    return false;
}

// /api/title?id=4D5307E6 -> what the board would show for this ID
void HandleTitle() {
    uint32_t id;
    if (!ArgId(&id)) return;
    std::string n, d;
    gamedb::Source src = gamedb::Lookup(SentryCustomTitles(), id, &n, &d);
    const char* names[] = { "none", "builtin", "custom" };
    server.send(200, "application/json", TitleJson(id, n, d, names[src]));
}

// /api/custom -> all custom titles as JSON
void HandleCustomList() {
    String j = "[";
    bool first = true;
    for (const gamedb::Title& t : SentryCustomTitles().All()) {
        if (!first) j += ',';
        first = false;
        j += TitleJson(t.id, t.name, t.dev, "custom");
    }
    j += "]";
    server.send(200, "application/json", j);
}

void SendSaved(bool changed) {
    if (!changed) { server.send(400, "text/plain", "Nothing changed"); return; }
    if (!SentrySaveCustomTitles()) { server.send(500, "text/plain", "Couldn't save to flash"); return; }
    server.send(200, "text/plain", "Saved");
}

// POST /api/custom/set  id, name, dev
void HandleCustomSet() {
    uint32_t id;
    if (!ArgId(&id)) return;
    if (!gamedb::Clean(server.arg("name").c_str()).size()) { server.send(400, "text/plain", "Enter a game name"); return; }
    bool ok = SentryCustomTitles().Set(id, server.arg("name").c_str(), server.arg("dev").c_str());
    if (!ok) { server.send(400, "text/plain", "The custom list is full"); return; }
    SendSaved(true);
}

// POST /api/custom/del  id
void HandleCustomDel() {
    uint32_t id;
    if (!ArgId(&id)) return;
    SendSaved(SentryCustomTitles().Remove(id));
}

// POST /api/custom/import  text = lines in gamelist.txt format (added to / replacing existing ones)
void HandleCustomImport() {
    const String& text = server.arg("text");
    if (text.length() > 32 * 1024) { server.send(400, "text/plain", "File too big (32 KB max, about 600 titles)"); return; }   // the body is held in RAM a few times over while parsed
    gamedb::Custom incoming;
    size_t n = incoming.Load(text.c_str());
    size_t added = 0;
    for (const gamedb::Title& t : incoming.All())
        added += SentryCustomTitles().Set(t.id, t.name, t.dev) ? 1 : 0;
    if (!added) { server.send(400, "text/plain", n ? "The custom list is full" : "No title lines found (format: ID*Game*Developer)"); return; }
    if (!SentrySaveCustomTitles()) { server.send(500, "text/plain", "Couldn't save to flash"); return; }
    server.send(200, "text/plain", String("Imported ") + added + " titles");
}

// /custom.txt -> download, same format as gamelist.txt
void HandleCustomDownload() {
    server.sendHeader("Content-Disposition", "attachment; filename=custom_titles.txt");
    server.send(200, "text/plain", SentryCustomTitles().Save().c_str());
}

// ---- firmware upload --------------------------------------------------------
bool PasswordOk() {
    if (strlen(OTA_PASSWORD) == 0) return true;
    return server.header("X-OTA-Password") == OTA_PASSWORD;
}

void HandleUpdateDone() {
    if (!g_otaAuthorized) { server.send(401, "text/plain", "Wrong update password"); return; }
    if (Update.hasError()) {
        server.send(500, "text/plain", g_otaError.length() ? g_otaError : String("Update failed"));
        DisplayOtaDone(false, g_otaError.c_str());
        return;
    }
    server.send(200, "text/plain", "OK");
    DisplayOtaDone(true, "Restarting...");
    delay(600);
    ESP.restart();
}

void HandleUpdateUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        g_otaError = "";
        g_otaAuthorized = PasswordOk();
        if (!g_otaAuthorized) return;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { g_otaError = Update.errorString(); return; }
        DisplayOtaProgress(0);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!g_otaAuthorized || !Update.isRunning()) return;
        if (Update.write(up.buf, up.currentSize) != up.currentSize) g_otaError = Update.errorString();
        size_t total = server.clientContentLength();
        if (total) DisplayOtaProgress((int)min<size_t>(99, up.totalSize * 100 / total));
    } else if (up.status == UPLOAD_FILE_END) {
        if (!g_otaAuthorized || !Update.isRunning()) return;
        if (Update.end(true)) DisplayOtaProgress(100);
        else g_otaError = Update.errorString();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        g_otaError = "Upload aborted";
    }
}

} // namespace

void WebBegin(const char* networkDescription) {
    g_net = networkDescription;
    const char* headers[] = { "X-OTA-Password" };
    server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, HandleRoot);
    server.on("/serialdata", HTTP_GET, HandleSerialData);
    server.on("/autoscroll", HTTP_GET, HandleAutoScroll);
    server.on("/clear", HTTP_GET, HandleClear);
    server.on("/api/status", HTTP_GET, HandleStatus);
    server.on("/api/led", HTTP_GET, HandleLed);
    server.on("/api/mode", HTTP_GET, HandleMode);
    server.on("/api/title", HTTP_GET, HandleTitle);
    server.on("/api/custom", HTTP_GET, HandleCustomList);
    server.on("/api/custom/set", HTTP_POST, HandleCustomSet);
    server.on("/api/custom/del", HTTP_POST, HandleCustomDel);
    server.on("/api/custom/import", HTTP_POST, HandleCustomImport);
    server.on("/custom.txt", HTTP_GET, HandleCustomDownload);
    server.on("/update", HTTP_POST, HandleUpdateDone, HandleUpdateUpload);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();
}

void WebLoop() { server.handleClient(); }
