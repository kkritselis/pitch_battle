#include "lcd_screen.h"

#if ENABLE_LCD

#include <Arduino_GFX_Library.h>
#include <AnimatedGIF.h>

#include "esp_screen_gif.h"

static Arduino_GFX *lcdGfx = nullptr;
static AnimatedGIF lcdGif;
static bool lcdGifReady = false;
static uint32_t lcdNextFrameMs = 0;

static void espScreenGifDraw(GIFDRAW *pDraw) {
  if (lcdGfx == nullptr) {
    return;
  }

  uint8_t *pixels = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {
    for (int x = 0; x < pDraw->iWidth; x++) {
      if (pixels[x] == pDraw->ucTransparent) {
        pixels[x] = pDraw->ucBackground;
      }
    }
    pDraw->ucHasTransparency = 0;
  }

  const int y = pDraw->iY + pDraw->y;
  if (pDraw->ucHasTransparency) {
    lcdGfx->drawIndexedBitmap(
      pDraw->iX,
      y,
      pixels,
      pDraw->pPalette,
      pDraw->ucTransparent,
      pDraw->iWidth,
      1
    );
  } else {
    lcdGfx->drawIndexedBitmap(
      pDraw->iX,
      y,
      pixels,
      pDraw->pPalette,
      pDraw->iWidth,
      1
    );
  }
}

static bool lcdOpenEspScreenGif() {
  return lcdGif.open(
           (uint8_t *)ESP_SCREEN_GIF,
           ESP_SCREEN_GIF_BYTES,
           espScreenGifDraw
         ) != 0;
}

void lcdScreenInit(Arduino_GFX *display) {
  lcdGfx = display;
}

void lcdScreenStart() {
  if (lcdGfx == nullptr) {
    return;
  }

  lcdGif.begin(GIF_PALETTE_RGB565_LE);
  lcdGifReady = lcdOpenEspScreenGif();
  lcdNextFrameMs = 0;

  if (lcdGifReady) {
    Serial.print("LCD esp_screen.gif opened ");
    Serial.print(lcdGif.getCanvasWidth());
    Serial.print("x");
    Serial.println(lcdGif.getCanvasHeight());

    int frameDelayMs = 10;
    lcdGif.playFrame(false, &frameDelayMs);
    if (frameDelayMs < 10) {
      frameDelayMs = 10;
    }
    lcdNextFrameMs = millis() + (uint32_t)frameDelayMs;
  } else {
    Serial.print("LCD esp_screen.gif open failed err=");
    Serial.println(lcdGif.getLastError());
    lcdGfx->fillScreen(BLACK);
    lcdGfx->setTextColor(WHITE);
    lcdGfx->setTextSize(1);
    lcdGfx->setCursor(24, 112);
    lcdGfx->print("LCD GIF failed");
  }
}

void lcdScreenLoop() {
  if (!lcdGifReady || lcdGfx == nullptr) {
    return;
  }

  if (millis() < lcdNextFrameMs) {
    return;
  }

  int frameDelayMs = 10;
  const int rc = lcdGif.playFrame(false, &frameDelayMs);
  if (rc <= 0) {
    lcdGif.reset();
    if (!lcdOpenEspScreenGif()) {
      lcdGifReady = false;
      Serial.println("LCD esp_screen.gif loop reopen failed");
      return;
    }
  }

  if (frameDelayMs < 10) {
    frameDelayMs = 10;
  }
  lcdNextFrameMs = millis() + (uint32_t)frameDelayMs;
}

#endif
