#pragma once

#define AP_SSID "PitchBattle"
#define AP_PASSWORD "pitchbattle"
#define LOCAL_URL "http://192.168.4.1"

// Set to 0 if the display pin mapping is wrong and you just want to test the web server.
#define ENABLE_LCD 1

#define TFT_MOSI 7

#define TFT_SCK  6

#define TFT_CS   10

#define TFT_DC   2

#define TFT_RST  -1

#define TFT_BL   3

#define SCREEN_W 240
#define SCREEN_H 240

// Set to 0 to run without iPixel BLE (web server and LCD only).
#define ENABLE_IPIXEL 1

// Devices advertise as LED_BLE_<id>. Leave IPIXEL_MAC empty to scan by name.
#define IPIXEL_DEVICE_PREFIX "LED_BLE_"
// Leave empty unless your display uses a different advertised name.
#define IPIXEL_ALT_PREFIX ""
#define IPIXEL_MAC ""

// How long each BLE scan pass runs.
#define IPIXEL_SCAN_SECONDS 8

// Keep trying BLE before Wi-Fi starts. QR code appears after this window.
#define IPIXEL_BOOT_WAIT_MS 18000

// Manufacturer company ID seen on LED_BLE_2D84B28A advertisements.
#define IPIXEL_MANUFACTURER_ID 0x5254

// After Wi-Fi starts, keep looking for the display until logo is shown.
#define IPIXEL_BACKGROUND_RETRY_MS 30000

// Stored animation slots (uploaded during Mac prototype work).
#define IPIXEL_SLOT_HOMERUN 1
#define IPIXEL_SLOT_TRIPLE 2
#define IPIXEL_SLOT_DOUBLE 3
#define IPIXEL_SLOT_SINGLE 4
#define IPIXEL_SLOT_WALK 5
#define IPIXEL_SLOT_BALL 6
#define IPIXEL_SLOT_FOUL 7
#define IPIXEL_SLOT_FLYOUT 8
#define IPIXEL_SLOT_SCOREBOARD 9
#define IPIXEL_SLOT_LOGO 10
