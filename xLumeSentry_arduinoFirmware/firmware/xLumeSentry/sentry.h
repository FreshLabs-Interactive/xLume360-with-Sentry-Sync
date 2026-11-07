// xLume + Sentry - Sentry half: console status, game lookup, ESP-NOW, web log
#pragma once
#include <Arduino.h>

// Must stay byte-for-byte identical to the SentrySync4 receiver's struct so
// the existing controller/remote screen keeps working without reflashing.
typedef struct struct_message {
    float gTemp;
    float cTemp;
    float eTemp;
    float mTemp;
    bool sd;            // always true now: the game list is built in (false shows an SD warning on the remote)
    bool uart;
    bool tidSent;
    char gameTitle[32];
    char developer[32];
    char tTitleID[32];
    char xboxipAdd[32];
} struct_message;

struct SentryState {
    float temps[4];          // CPU, GPU, eDRAM, Mobo (deg C)
    bool haveTemps;
    bool uartMode;
    bool titleKnown;         // the current title ID was found in the game list
    char titleId[12];
    char gameTitle[32];
    char developer[32];
    char xboxIp[20];
    uint32_t lastLineMs;     // when the console last said anything
};

void SentryBegin(bool espNowOk);
void SentryHandleLine(const char* line, uint32_t now);   // every UART line, from loop()
void SentryLoop(uint32_t now);                           // periodic work, from loop()
const SentryState& SentryGet();
void SentrySetUartMode(bool uart);                       // web page / console command

// Game list (built in + custom titles saved in flash). See gamedb.h.
namespace gamedb { class Custom; }
gamedb::Custom& SentryCustomTitles();
bool SentrySaveCustomTitles();        // writes /custom.txt; also refreshes the current game's name
bool SentryStorageOk();               // the flash file system mounted

// Web console buffer (same behaviour as SentrySync's /serialdata)
void WebLogAdd(const char* line);
String WebLogTake();
void WebLogClear();
