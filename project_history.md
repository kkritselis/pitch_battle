# Pitch Battle — Project History

This document records how the project evolved from the Mac prototype to the current ESP32 firmware, including completed milestones and technical findings from bring-up.

For how to build, run, and use the game today, see [README.md](README.md).

---

## Mac prototype to ESP32 rebuild

This repo is a **rebuild** of an earlier Mac-based prototype.

|                   | Mac prototype (previous)     | This repo (current)                                             |
| ----------------- | ---------------------------- | --------------------------------------------------------------- |
| Host              | Node.js on a Mac             | ESP32-C3                                                        |
| Web app           | Served from Node             | Embedded in firmware (`src/main.cpp`)                           |
| Wi-Fi             | Mac hotspot or local network | ESP32 access point + captive portal                             |
| Round LCD QR code | Not part of prototype        | GC9A01 display on ESP32                                         |
| Game logic        | Node                         | `resolvePlay()` in firmware                                     |
| iPixel display    | Node BLE client (working)    | ESP32 BLE client with live scoreboard                           |
| Animation uploads | Done via Mac tooling         | GIF slots via `setup/storeImages.py`; scoreboard live on slot 5 |

The Mac prototype proved out multiplayer flow, play resolution, and iPixel BLE control. This repo moves that stack onto the ESP32 so the game can run standalone.

---

## Completed milestones

### Phase 1 — iPixel BLE on ESP32

- [x] Add NimBLE-Arduino to `platformio.ini`
- [x] Scan for and connect to the iPixel on boot
- [x] Discover the FA02 write characteristic
- [x] Implement `showIPixelResult()` using the slot display command
- [x] Show slot 10 (logo) at startup

**Outcome:** Logo appears on the iPixel automatically after ESP32 boot, with no Mac involved.

**Key findings:**

- The display ignores NimBLE's UUID-derived write handle for `fa02` (`0x0009`) even though queued writes report success. Raw ATT handle `0x0006` is the working command handle for this unit.
- Commands are sent with GATT write-with-response to handle `0x0006`, matching the Mac transport behavior closely enough for stored-slot control.
- `Serial` is routed to USB via `ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` for boot logging.
- The target unit (`LED_BLE_2D84B28A`) is pinned by address in `IPIXEL_MAC` (`config.h`) for direct connect; clear it to scan by name.
- The current board has a small internal antenna already installed; treat iPixel issues as protocol/coexistence problems until diagnostic modes isolate the failure.

### Phase 2 — Result animations

- [x] `resolvePlay()` returns a structured `PlayResult` (text + image token)
- [x] Map outcomes to slots: homerun, double, single, foul, strike, flyout
- [x] Trigger the animation immediately after `resolvePlay()` completes
- [x] Add a "Next pitch" button in the web UI that calls `/api/reset`
- [x] Parse JSON responses on the client (no raw JSON flash; clean status line)
- [x] iPixel reliably switches animation per play

**Outcome:** Pitch/swing lock, resolution, result text on both phones, and "Next pitch" reset all work. Resolved plays queue iPixel work for the main loop instead of calling BLE from the HTTP request handler.

**Transport notes:**

- `show_slot` bytes match `pypixelcolor` exactly: `0x07 0x00 0x08 0x80 0x01 0x00 <slot>`.
- `pypixelcolor` sends through Bleak with `response=True`. On ESP32/NimBLE, write-with-response works when sent directly to raw handle `0x0006`; the UUID-derived handle `0x0009` fails.
- Result commands are loop-driven and non-blocking. During each result burst, the firmware temporarily prefers BLE coexistence and restores balance after the burst.
- If the requested slot is empty, the device falls back to cycling through populated slots.
- Diagnostic mode 1 confirmed slots 1, 4, 7, and 10 switch correctly with `IPIXEL_RAW_WRITE_HANDLE 0x0006`.
- Do not call blocking BLE reads/writes from the web-server request handler task; a diagnostic `readValue()` there caused a load-access-fault crash.

### Phase 3 — Scoreboard state

- [x] Inning (top/bottom)
- [x] Home and away score
- [x] Balls, strikes, outs
- [x] Base runners
- [x] Expose state through `/api/state`

**Outcome:** Count, outs, score, inning half, and base runners advance across multiple pitch/swing cycles. `/api/reset` starts the next pitch while preserving the scoreboard; `/api/new-game` resets the full game state.

**Rules implemented:**

- Singles advance runners one base, doubles two bases, and home runs score all runners plus the batter.
- Fouls add a strike only when the batter has fewer than two strikes.
- Three strikes or any out result records an out; three outs clear the bases and advance the half-inning.

### Phase 4 — iPixel scoreboard

- [x] Flow: result animation or scroll text plays, then scoreboard returns
- [x] Configurable delay via `IPIXEL_SCOREBOARD_RETURN_MS`
- [x] Render live score/count/base runner pixels into the iPixel scoreboard
- [x] Push generated scoreboard PNG to slot 5 and call `show_slot(5)`
- [x] Treat the missing notify ACK as success (image displays without it)
- [x] Compress the PNG on-device so the panel's decoder accepts it
- [x] Scroll text for walk and strikeout instead of stored GIF slots

**Outcome:** After each play, the iPixel shows the result (GIF slot or scroll text), waits 5 seconds, then displays the live scoreboard from slot 5. Using a numbered slot for the scoreboard keeps stored hit/out animations working alongside live updates.

**Scoreboard template:**

- Balls, strikes, and outs render as single 3x5 digits on the left edge.
- Occupied first, second, and third base boxes render red in the center diamond.
- The right-side grid renders visitor and home rows with inning 1–3 runs and a total in the far-right box.

**Image push debugging (three fixes in sequence):**

1. **Window header size.** The 13-byte image window header was being counted as 11 bytes, so the length prefix was off by 2 and the payload truncated. Fixing this let the full frame reach the panel.
2. **Notify ACK.** The device never returns a notify ACK on the ESP32 link, so the firmware was timing out and overwriting the image with a stored slot. It now treats a fully-written window as success (`IPIXEL_REQUIRE_IMAGE_ACK 0`).
3. **Compression.** The panel rejects an uncompressed (stored) PNG. The ROM miniz compressor cannot allocate on the ESP32-C3, so the firmware uses a built-in fixed-Huffman DEFLATE encoder (~150–300 byte compressed PNG).

**Scoreboard slot regression:** Pushing the scoreboard with `save_slot=0` (direct overlay) caused stored GIF animations to stop responding to `show_slot` commands even though BLE writes succeeded. Moving the live scoreboard to slot 5 restored animation playback.

### Team join and role enforcement

- [x] Single Join Game screen; first phone = Home, second = Away
- [x] Token stored in `localStorage` for reload persistence
- [x] Server rejects pitch/swing from the wrong team (`403`)

### Walks and strikeouts (partial Phase 5)

- [x] Four balls = walk (advance batter, reset count)
- [x] Three strikes = strikeout (out, reset count)
- [x] iPixel scroll text for `"WALK"` and `"STRIKEOUT"` via pre-encoded frames in `include/ipixel_scroll_text.h`

---

## End goal (achieved)

The standalone game loop works end to end:

1. ESP32 powers on
2. Logo appears on the iPixel
3. QR code appears on the round LCD
4. Players scan, join Wi-Fi, and tap Join Game (Home then Away)
5. The pitching and batting teams lock their choices for the half
6. ESP32 resolves the play
7. iPixel shows the result animation or scroll text (walk/strikeout)
8. Scoreboard updates on slot 5
9. Play continues through full innings

No laptop, cloud service, or external server required during gameplay.

---

## iPixel diagnostic history

Normal gameplay uses `IPIXEL_DIAGNOSTIC_MODE 0`, `IPIXEL_WRITE_WITH_RESPONSE 1`, `IPIXEL_DIAG_HANDLE_SCAN 0`, and `IPIXEL_RAW_WRITE_HANDLE 0x0006`.

When BLE commands queue successfully but the display ignores them, this was the test order used during bring-up:

1. Set `IPIXEL_DIAGNOSTIC_MODE` to `1` and flash. Wi-Fi is disabled; firmware cycles slots 1, 4, 7, and 10 from `loop()`.
2. If mode 1 logs `rc=0` but the display ignores slots, set `IPIXEL_USE_AE_CHANNEL` to `1` and retest (switches from `fa02`/`fa03` to `ae01`/`ae02`).
3. If the channel switch still fails, compare `IPIXEL_WRITE_WITH_RESPONSE` `0` vs `1` in mode 1.
4. Set `IPIXEL_REQUIRE_SLOT_ACK` to `1` in mode 1. A timeout means the display is not accepting that channel/write mode.
5. Set `IPIXEL_DIAG_HANDLE_SCAN` to `1` in mode 1. Writes `show_slot` to handles `0x0005` through `0x000F`.
6. Set `IPIXEL_DIAGNOSTIC_MODE` to `2` — Wi-Fi on, same slot cycle, no phone input. Isolates Wi-Fi/BLE coexistence.
7. Return to mode `0` for normal two-phone gameplay.

**Notify channel:** The iPixel notify characteristic (`fa03`) has never produced a packet on the ESP32 connection, even for the device-info handshake. Image and slot ACKs are not waited on; the panel still receives and displays pushed content.
