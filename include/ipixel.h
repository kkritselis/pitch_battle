#pragma once

#include <Arduino.h>
#include "config.h"
#include "scoreboard.h"

#if ENABLE_IPIXEL

void ipixelBegin();
void ipixelNotifyWifiActive();
void ipixelLoop();
bool ipixelIsConnected();
bool ipixelLogoDisplayed();
uint8_t ipixelDevicesSeen();
const char *ipixelAddress();
bool ipixelShowSlot(uint8_t slot);
bool ipixelShowSlotAtHandle(uint8_t slot, uint16_t handle, bool waitForAck);
bool ipixelBusy();
void showIPixelScoreboard(const ScoreboardState &state);
void showIPixelResult(const String &imageName, const ScoreboardState &state);

#else

inline void ipixelBegin() {}
inline void ipixelNotifyWifiActive() {}
inline void ipixelLoop() {}
inline bool ipixelIsConnected() { return false; }
inline bool ipixelLogoDisplayed() { return false; }
inline uint8_t ipixelDevicesSeen() { return 0; }
inline const char *ipixelAddress() { return ""; }
inline bool ipixelShowSlot(uint8_t) { return false; }
inline bool ipixelShowSlotAtHandle(uint8_t, uint16_t, bool) { return false; }
inline bool ipixelBusy() { return false; }
inline void showIPixelScoreboard(const ScoreboardState &) {}
inline void showIPixelResult(const String &, const ScoreboardState &) {}

#endif
