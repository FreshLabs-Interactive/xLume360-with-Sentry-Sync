// =============================================================================
//  xLume + Sentry  -  one ESP32 for both projects
//
//  * xLume:  LED strip effects controlled from the console (xLume 360 app/plugin)
//  * Sentry: 2" TFT status screen, SD game list, web console, ESP-NOW remote
//
//  Both listen to the same wire: the Xbox 360 debug UART (DbgPrint output).
//    "[xLume] - ..."  lines -> LED engine      (led_engine.cpp)
//    "[Sentry] ..."   lines -> status screen   (sentry.cpp, display.cpp)
//
//  Based on SentrySync4_Sender_w2inTFT by Element / JFresh and the xLume V2
//  firmware protocol by Element18592 & xhjuu. Combined by FreshLabs Interactive.
//
//  Board: "ESP32 Dev Module"   Partition scheme: "Minimal SPIFFS (1.9MB APP with OTA)"
//  Settings: config.h  (Xbox TX on RX0 by default, like the original Sentry board)
// =============================================================================
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_now.h>
#include "config.h"
#include "protocol.h"
#include "led_engine.h"
#include "sentry.h"
#include "display.h"
#include "web.h"

extern const char* const kFirmwareVersion = "1.0.0";

namespace {

const size_t kLineMax = 200;
QueueHandle_t g_lines = nullptr;     // UART lines for loop() (Sentry, web log)
SPIClass g_sdSpi(HSPI);              // SD on its own bus; the TFT uses VSPI
char g_netText[64] = "";

// The port the Xbox talks on (see XBOX_ON_RX0 in config.h).
#if XBOX_ON_RX0
HardwareSerial& g_xbox = Serial;     // RX0 / GPIO3, shared with the USB chip
#else
HardwareSerial& g_xbox = Serial2;    // RX2 / GPIO16
#endif

// Reads the Xbox UART on core 0. xLume commands go straight to the LED task
// (so the lights react even while the screen is busy); every line is also
// handed to loop() for the Sentry side and the web console.
void UartTask(void*) {
    char buf[kLineMax];
    size_t len = 0;
    for (;;) {
        while (g_xbox.available()) {
            int c = g_xbox.read();
            if (c == '\n') {
                buf[len] = '\0';
                if (len && buf[len - 1] == '\r') buf[--len] = '\0';
                if (len) {
                    const char* body = xls::XlBody(buf);
                    if (body) LedPost(body);
                    xQueueSend(g_lines, buf, 0);   // drop if loop() is far behind
                }
                len = 0;
            } else if (len < kLineMax - 1) {
                buf[len++] = (char)c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool ConnectWifi() {
    if (!strcmp(WIFI_SSID, "NETWORK NAME") || !strlen(WIFI_SSID)) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
    return WiFi.status() == WL_CONNECTED;
}

bool StartEspNow() {
#if ESPNOW_ENABLED
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return false;
    }
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, RECEIVER_MAC, 6);
    peer.channel = 0;          // current Wi-Fi channel
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("ESP-NOW: failed to add receiver");
        return false;
    }
    return true;
#else
    return false;
#endif
}

void StartOta() {
    ArduinoOTA.setHostname(HOSTNAME);
    if (strlen(OTA_PASSWORD)) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { DisplayOtaProgress(0); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        if (total) DisplayOtaProgress((int)(done * 100ULL / total));
    });
    ArduinoOTA.onEnd([]() { DisplayOtaDone(true, "Restarting..."); });
    ArduinoOTA.onError([](ota_error_t e) {
        char b[32];
        snprintf(b, sizeof(b), "Error %u", (unsigned)e);
        DisplayOtaDone(false, b);
    });
    ArduinoOTA.begin();
}

} // namespace

void setup() {
#if XBOX_ON_RX0
    // RX0: the Xbox feeds UART0's receive pin; our log lines still go out of
    // TX0 to the USB chip (the Xbox never sees them).
    Serial.setRxBufferSize(4096);
    Serial.begin(XBOX_BAUD);
#else
    Serial.begin(115200);                         // USB: logs / serial monitor
    Serial2.setRxBufferSize(4096);
    Serial2.begin(XBOX_BAUD, SERIAL_8N1, XBOX_RX2_PIN, -1);
#endif
    Serial.println("\nxLume + Sentry starting");

    LedBegin();                                   // LEDs come back to the saved look right away
    DisplayBegin();

    g_sdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    bool sdOk = SD.begin(SD_CS_PIN, g_sdSpi);
    Serial.println(sdOk ? "SD card OK" : "SD card not found");

    bool sta = ConnectWifi();
    String ip;
    if (sta) {
        ip = WiFi.localIP().toString();
        snprintf(g_netText, sizeof(g_netText), "%s  (%s.local)", ip.c_str(), HOSTNAME);
        DisplayShowBoot("Sentry WebServer is", "Ready At IP Address:", ip.c_str(), "xLume LEDs ready");
    } else {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        ip = WiFi.softAPIP().toString();
        snprintf(g_netText, sizeof(g_netText), "Wi-Fi %s  %s", AP_SSID, ip.c_str());
        DisplayShowBoot("No router - join Wi-Fi:", AP_SSID, AP_PASSWORD, ip.c_str());
    }
    Serial.printf("Web page: http://%s/\n", ip.c_str());
    DisplaySetWebAddress(("web: " + ip).c_str());
    if (!sdOk) DisplayShowSdMissing();

    bool espNow = StartEspNow();
    SentryBegin(sdOk, espNow);
    WebBegin(g_netText);
    StartOta();                                   // also starts mDNS (xlume-sentry.local)

    g_lines = xQueueCreate(64, kLineMax);
    xTaskCreatePinnedToCore(UartTask, "xbox-uart", 4096, nullptr, 3, nullptr, 0);
}

void loop() {
    ArduinoOTA.handle();
    WebLoop();

    uint32_t now = millis();
    char line[kLineMax];
    for (int i = 0; i < 16 && xQueueReceive(g_lines, line, 0) == pdTRUE; ++i)
        SentryHandleLine(line, now);

    SentryLoop(now);
    DisplayUpdate(now);
    delay(2);
}
