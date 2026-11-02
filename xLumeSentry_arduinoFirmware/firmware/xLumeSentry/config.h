// =============================================================================
//  xLume + Sentry  -  user settings
//  Edit this file before flashing. Everything else can stay as it is.
// =============================================================================
#pragma once

// ---- Wi-Fi ------------------------------------------------------------------
// Your router. If it can't connect within ~10 s the board starts its own
// access point instead (below), so the web page is always reachable.
#define WIFI_SSID        "SSID"
#define WIFI_PASSWORD    "PASSWORD"

// Fallback access point (only used when the router connection fails).
#define AP_SSID          "xLume-Sentry"
#define AP_PASSWORD      "xlume360"          // at least 8 characters

// Name on the network: http://xlume-sentry.local and in the Arduino IDE port list.
#define HOSTNAME         "xlume-sentry"

// Password for firmware updates (Arduino IDE network port AND the web page
// upload). Change it. Leave "" to allow updates without a password.
#define OTA_PASSWORD     ""

// ---- Sentry remote screen (ESP-NOW) ------------------------------------------
// MAC address of your SentrySync4 receiver (the ESP32-C3 in the controller).
// Same value you used in SentrySync4_Sender_w2inTFT. Set ESPNOW_ENABLED to 0
// if you don't have a remote screen.
#define ESPNOW_ENABLED   1
static const uint8_t RECEIVER_MAC[6] = { 0xa0, 0x85, 0xe3, 0x4c, 0xf0, 0x28 };

// ---- Pins (ESP32-WROOM-32 dev board) -----------------------------------------
// Xbox debug UART: wire the console's TX point (J2C3.2 slim / J2B1.2 phat) here.
//
//   XBOX_ON_RX0 1 : Xbox TX on RX0 (GPIO3) - same wire as the original Sentry
//                   board. Update with OTA. To flash over USB, disconnect the
//                   Xbox TX wire first (the USB chip also uses RX0).
//   XBOX_ON_RX0 0 : Xbox TX on RX2 (GPIO16, set below). USB stays usable for
//                   flashing and the serial monitor with the Xbox connected.
#define XBOX_ON_RX0      1
#define XBOX_RX2_PIN     16
#define XBOX_BAUD        115200

// LED strip data (WS2812B). Use a 74AHCT125 level shifter, or at least a
// 330 ohm resistor in series, between this pin and the strip's DIN.
#define LED_DATA_PIN     27
#define LED_MAX_COUNT    300                 // same limit as the xLume firmware
#define LED_TYPE         WS2812B
#define LED_COLOR_ORDER  GRB                 // change to RGB/BRG if colours look swapped

// Optional hard current limit for the strip in mA (0 = off). The xLume 360
// app/plugin already caps brightness; this is a second safety net.
#define LED_MAX_MILLIAMPS 0

// SD card (gamelist.txt). On its own SPI bus so it never fights the screen.
#define SD_CS_PIN        5
#define SD_SCK_PIN       18
#define SD_MISO_PIN      19
#define SD_MOSI_PIN      23

// The TFT pins are set in the TFT_eSPI library setup file (same as Sentry):
//   MOSI 21, SCLK 22, CS 15, DC 2, RST 4   - see README "TFT_eSPI setup".

// Sentry's 2" ST7789 panel takes colours in blue-green-red order.
// Set to 0 if reds and blues come out swapped on your screen.
#define TFT_SWAP_RED_BLUE 1
