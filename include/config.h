#pragma once

#define AP_SSID "PitchBattle"
#define AP_PASSWORD "pitchbattle"
#define LOCAL_URL "http://192.168.4.1"

// Regulation length; game ends after the bottom of this inning is complete.
#define GAME_INNINGS 3

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

// Round LCD bezel clips the edges; shrink and center on-screen content.
#define LCD_LAYOUT_SCALE_PCT 85

// Set to 0 to run without iPixel BLE (web server and LCD only).
#define ENABLE_IPIXEL 1

// iPixel troubleshooting mode:
// 0 = normal game
// 1 = BLE-only slot cycle test (no Wi-Fi)
// 2 = Wi-Fi-on idle slot cycle test (no phone input needed)
#define IPIXEL_DIAGNOSTIC_MODE 0

#define IPIXEL_DIAG_SLOT_INTERVAL_MS 4000
#define IPIXEL_LOG_GATT 1
// 0 = ESP32 direct write-no-response path (current stable path)
// 1 = Mac pypixelcolor-style GATT write-with-response experiment
#define IPIXEL_WRITE_WITH_RESPONSE 1
// 0 = documented pypixelcolor FA02/FA03 channel
// 1 = alternate AE01/AE02 vendor channel exposed by this iPixel
#define IPIXEL_USE_AE_CHANNEL 0
// 0 = fire and forget slot commands
// 1 = wait for the notify ACK that pypixelcolor expects for show_slot
#define IPIXEL_REQUIRE_SLOT_ACK 0
// Diagnostic-only: write show_slot directly to raw ATT handles, useful when
// UUID-based characteristic writes queue successfully but the display ignores
// them. Known low-level iPixel docs mention handle 0x0006 on some variants.
#define IPIXEL_DIAG_HANDLE_SCAN 0
#define IPIXEL_DIAG_HANDLE_START 0x0005
#define IPIXEL_DIAG_HANDLE_END 0x000F
// 0 = use characteristic handle from NimBLE
// nonzero = write all iPixel commands to this raw ATT handle
#define IPIXEL_RAW_WRITE_HANDLE 0x0006
// Image uploads may use a different FA02 value handle than show_slot commands.
// 0 = use IPIXEL_RAW_WRITE_HANDLE for image frames too.
#define IPIXEL_IMAGE_RAW_WRITE_HANDLE 0
// This unit accepts the image window and displays it, but never returns a notify
// ACK on the ESP32 link. 0 = treat a fully-written window as success even when no
// ACK arrives (avoids the slot-9 fallback overwriting the live image).
#define IPIXEL_REQUIRE_IMAGE_ACK 0
// Diagnostic: send a known-good Pillow-compressed scoreboard PNG from flash.
// If this ACKs, image framing works and runtime PNG compression is the blocker.
#define IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG 0

// Devices advertise as LED_BLE_<id>. Leave IPIXEL_MAC empty to scan by name.
#define IPIXEL_DEVICE_PREFIX "LED_BLE_"
// Leave empty unless your display uses a different advertised name.
#define IPIXEL_ALT_PREFIX ""
// Pinned to this unit (LED_BLE_2D84B28A) for direct connect; clear to scan.
#define IPIXEL_MAC "08:bf:2d:84:b2:8a"

// How long each BLE scan pass runs.
#define IPIXEL_SCAN_SECONDS 8

// Keep trying BLE before Wi-Fi starts. QR code appears after this window.
#define IPIXEL_BOOT_WAIT_MS 18000

// Manufacturer company ID seen on LED_BLE_2D84B28A advertisements.
#define IPIXEL_MANUFACTURER_ID 0x5254

// After Wi-Fi starts, keep looking for the display until logo is shown.
#define IPIXEL_BACKGROUND_RETRY_MS 30000

// After showing a result animation, return the iPixel to the stored scoreboard slot.
#define IPIXEL_SCOREBOARD_RETURN_MS 5000

// Stored animation slots (uploaded during Mac prototype work).
#define IPIXEL_SLOT_HOMERUN 1
#define IPIXEL_SLOT_TRIPLE 2
#define IPIXEL_SLOT_DOUBLE 3
#define IPIXEL_SLOT_SINGLE 4
#define IPIXEL_SLOT_SCOREBOARD 5
#define IPIXEL_SLOT_BALL 6
#define IPIXEL_SLOT_FOUL 7
#define IPIXEL_SLOT_FLYOUT 8
#define IPIXEL_SLOT_GROUNDOUT 9
#define IPIXEL_SLOT_STRIKE 10
