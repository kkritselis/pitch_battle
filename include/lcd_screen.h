#pragma once

#include "config.h"

#if ENABLE_LCD

class Arduino_GFX;

void lcdScreenInit(Arduino_GFX *display);
void lcdScreenStart();
void lcdScreenLoop();

#else

inline void lcdScreenInit(void *) {}
inline void lcdScreenStart() {}
inline void lcdScreenLoop() {}

#endif
