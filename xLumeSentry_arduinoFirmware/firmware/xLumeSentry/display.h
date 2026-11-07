// xLume + Sentry - 2" ST7789 TFT (320x240, landscape)
//
// Status screen = SentrySync background + temperatures (top left) + a new
// "Lighting" panel (bottom left) showing the current effect, colours,
// brightness and a live preview of the strip.
// Pop-up pages (boot, Xbox IP, Now Playing, firmware update) are
// timed instead of using delay(), so the LEDs and web page never freeze.
#pragma once
#include <Arduino.h>

void DisplayBegin();
void DisplayUpdate(uint32_t now);                 // call every loop()

void DisplayShowBoot(const char* line1, const char* line2, const char* line3, const char* line4);
void DisplayShowXboxIp(const char* ip);
void DisplayShowNowPlaying(const char* game, const char* titleId, const char* developer);
void DisplaySetUartMode(bool uart);
void DisplayUartLine(const char* line);
void DisplaySetWebAddress(const char* text);      // shown at the bottom of the lighting panel

// Firmware update screen. Drawn immediately (the update blocks loop()).
void DisplayOtaProgress(int percent);             // 0..100
void DisplayOtaDone(bool ok, const char* message);
