#pragma once

#include <Arduino.h>
#include "config.h"

#if ENABLE_IPIXEL

void ipixelBegin();
void ipixelNotifyWifiActive();
void ipixelLoop();
bool ipixelIsConnected();
bool ipixelLogoDisplayed();
uint8_t ipixelDevicesSeen();
const char *ipixelAddress();
bool ipixelShowSlot(uint8_t slot);
void showIPixelResult(const String &imageName);

#else

inline void ipixelBegin() {}
inline void ipixelNotifyWifiActive() {}
inline void ipixelLoop() {}
inline bool ipixelIsConnected() { return false; }
inline bool ipixelLogoDisplayed() { return false; }
inline uint8_t ipixelDevicesSeen() { return 0; }
inline const char *ipixelAddress() { return ""; }
inline bool ipixelShowSlot(uint8_t) { return false; }
inline void showIPixelResult(const String &) {}

#endif
