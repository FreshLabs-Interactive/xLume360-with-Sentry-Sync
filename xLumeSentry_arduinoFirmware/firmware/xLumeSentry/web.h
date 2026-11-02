// xLume + Sentry - web server (status, LED controls, UART console, firmware upload)
#pragma once
#include <Arduino.h>

void WebBegin(const char* networkDescription);
void WebLoop();
