// xLume + Sentry - LED engine (the xLume half)
//
// Runs in its own FreeRTOS task at ~60 fps. Commands arrive as xLume protocol
// text ("Color: 255, 0, 0", "Effects: 9", ...) from the Xbox UART and from the
// web page, through one queue, so both are handled identically.
// The look is saved to flash (NVS) 3 s after the last change, like the
// original firmware, and restored at power-up.
#pragma once
#include <stdint.h>

static const int LED_PREVIEW_DOTS = 12;

struct LedStatus {
    uint8_t effect;
    uint8_t brightness;
    uint8_t speed;
    uint16_t count;
    uint8_t c1[3];
    uint8_t c2[3];
    uint8_t preview[LED_PREVIEW_DOTS][3];   // sampled real pixel colours (before brightness)
    uint32_t changeCounter;                  // increments when any setting changes
};

void LedBegin();                         // call once from setup()
bool LedPost(const char* body);          // queue an xLume command body; false if queue full
void LedGetStatus(LedStatus* out);       // thread-safe snapshot
