# Pitch Battle — Project History

This document records how the project evolved from the Mac prototype to the current ESP32 firmware, including completed milestones and technical findings from bring-up.

For a quick overview, see [README.md](README.md). For API, hardware, and developer reference, see [details.md](details.md).

---

## Mac prototype to ESP32 rebuild

This repo is a **rebuild** of an earlier Mac-based prototype.

|                   | Mac prototype (previous)     | This repo (current)                                             |
| ----------------- | ---------------------------- | --------------------------------------------------------------- |
| Host              | Node.js on a Mac             | ESP32-C3                                                        |
| Web app           | Served from Node             | Embedded in firmware (`src/index.html` + JPG slices)            |
| Wi-Fi             | Mac hotspot or local network | ESP32 access point + captive portal                             |
| Round LCD         | Not part of prototype        | GC9A01 animated attract loop (`esp_screen.gif`)                 |
| Game logic        | Node                         | Weighted outcome lookup + count-based walks/strikeouts          |
| iPixel display    | Node BLE client (working)    | ESP32 BLE client with live scoreboard                           |
| Animation uploads | Done via Mac tooling         | GIF slots 1–4, 6–10 via `storeImages.py`; logo pushed directly; scoreboard live on slot 5 |

The Mac prototype proved out multiplayer flow, play resolution, and iPixel BLE control. This repo moves that stack onto the ESP32 so the game can run standalone.

---

## Completed milestones

### Phase 1 — iPixel BLE on ESP32

- [x] Add NimBLE-Arduino to `platformio.ini`
- [x] Scan for and connect to the iPixel on boot
- [x] Discover the FA02 write characteristic
- [x] Implement `showIPixelResult()` using the slot display command
- [x] Show logo on the iPixel at startup (originally slot 10; later moved to direct push — see below)

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
- Diagnostic mode 1 confirmed slots 1, 4, 7, and 10 switch correctly with `IPIXEL_RAW_WRITE_HANDLE 0x0006` (slot 10 is now strike, not logo).
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

### Walks and strikeouts

- [x] Four balls = walk (advance batter, reset count)
- [x] Three strikes = strikeout (out, reset count)
- [x] iPixel scroll text for `"WALK"` and `"STRIKEOUT"` via pre-encoded frames in `include/ipixel_scroll_text.h`

### Outcome lookup table

- [x] Authoritative outcomes in `setup/pitching-battle-outcomes.json` (81 pitch/swing combos, weighted responses)
- [x] `setup/generateOutcomes.py` → `include/pitch_outcomes_data.h`
- [x] `src/pitch_outcomes.cpp` replaces the old formula-based `resolvePlay()` comparison
- [x] Triple, ground out, and other hit/out types map to correct iPixel slots

**Outcome:** Each pitch/swing lock resolves through a lookup table that matches the Mac prototype's outcome design. Walks and strikeouts are still derived from the ball/strike count after a ball or strike outcome, not from the JSON table directly.

### Round LCD attract screen

- [x] Replace boot QR code with full-screen animated GIF (`setup/esp_screen.gif`)
- [x] `setup/generateEspScreenGif.py` → `include/esp_screen_gif.h`
- [x] `src/lcd_screen.cpp` plays the loop on the GC9A01 round display

**Outcome:** The round LCD shows a branded attract loop on boot instead of a static QR code. Players still join via the captive portal or `http://192.168.4.1`.

### Phone web UI redesign

- [x] Design mockup in `src/index.html` with six JPG background slices (`src/phone_*.jpg`, ~85 KB total)
- [x] Asset pipeline: `setup/generateWebAssets.py` → `include/web_index.h` + `include/phone_assets.h`
- [x] PlatformIO pre-build hook regenerates web assets before each compile
- [x] **Join screen** — header art + Join Game button
- [x] **Play screen** — live scoreboard, field diagram, PVP role text, pitch/swing pickers, lock-in button
- [x] **Result screen** — outcome text in the choice panel + Next Pitch (`/api/reset`)
- [x] Viewport scaling — fixed 953px-wide layout scaled to phone via `zoom` (Chrome) or `transform: scale()` + shell height
- [x] `layoutApp()` / `ResizeObserver` fixes join-screen height sticking after game reveal (288px crop bug)
- [x] Choice buttons — vertical stacked mockup style (class selectors; fixed `#id` vs `.class` mismatch)
- [x] Base runners — hidden by default, red when occupied
- [x] Result text sized for the result panel only (not PVP status line)

**Outcome:** Two phones can join (Home then Away), see live game state from `/api/state`, lock pitch/swing choices, and advance through at-bats with the new art-directed UI. Flash usage is ~93% of 1280 KB after embedding HTML and JPGs.

**Removed:** Inline `pageHtml()` string in `src/main.cpp`; the web UI is now edited in `src/index.html` and embedded at build time.

### iPixel scoreboard layout fixes

- [x] Scorebox-only template edits for rows 9–16 (x ≥ 46); left side (x < 46) preserved from original template
- [x] Removed full-width bottom rule that spanned all 96 pixels
- [x] Visit scores at y=2, home scores at y=10; 5px-tall score fill rects
- [x] Count colors: balls/outs yellow, strikes cyan (contrast with labels)
- [x] 2px divider between visit and home rows in the scorebox
- [x] Scoreboard artwork moved to `assets/ipixel/`; `setup/storeImages.py` loads slot assets from there
- [x] Ground out GIF on slot 9 (`setup/ground_out.gif`); fly out remains slot 8

**Outcome:** Live scoreboard on slot 5 renders correctly — strikes/outs/diamond on the left are intact, visit/home rows and totals align on the right, and count digits use readable contrasting colors.

**Regression fixed:** Early scorebox template edits wiped left-side rows, causing a strikes gap, clipped outs, and distorted base diamond. The fix merged the original left-side pixels with scorebox-only right-side changes.

### Outcome balance (extra balls and strikes)

- [x] `setup/generateOutcomes.py` injects **10% strike** and **5% ball** into each hit/out combo
- [x] Existing play weights in each combo scaled to 85% so totals remain 100
- [x] Combos that were already 100% strike/ball (miss scenarios) left unchanged

**Outcome:** Playtesting showed too many hits and outs relative to balls and strikes. Every pitch/swing pair now has a baseline chance of a called strike or ball before the weighted hit/out table is consulted.

### Three-inning game and end-of-game sequence

- [x] `GAME_INNINGS 3` in `include/config.h`
- [x] Game ends after the bottom of the 3rd — no advance to a 4th inning
- [x] `gameOverPending` / `gameOverActive` / `gameOverMessage` exposed in `/api/state`
- [x] Final play shows result; phone button reads **End Game**
- [x] Wrap-up: score banner on iPixel twice → reset state → clear tokens → `WiFi.softAPdisconnect(true)` → logo attract pushed again
- [x] Phone UI shows game-over message during reset

**Outcome:** A full three-inning session can be played start to finish with a defined ending. Players must reconnect to Wi-Fi to start a new game.

### Logo direct push and strike slot 10

- [x] **`strike.gif` on slot 10** — uploaded via `setup/storeImages.py`; firmware maps `"strike"` → `IPIXEL_SLOT_STRIKE`
- [x] **Logo removed from slot 10** — no longer uploaded or referenced as a stored slot
- [x] **`setup/generateLogoGif.py`** → `include/ipixel_logo_gif.h` — pre-built BLE windows from `setup/logo.gif` (~13 KB, two frames)
- [x] Logo pushed directly (`save_slot=0`) on boot and after game over; loops until the first at-bat of a session resolves
- [x] Attract ends when both players lock their first pitch/swing (`ipixelNotifyFirstPlayResolved()`)
- [x] **`IPIXEL_RAW_MESSAGE_MAX_BYTES`** — logo windows (~12 KB) exceeded the scoreboard PNG send limit (~6 KB); raw BLE frames now use a separate size cap

**Outcome:** Strike plays the correct animation (not the ball GIF or scroll text). Logo no longer competes with strike for slot 10. The logo GIF ships in firmware and does not require a Mac upload after each flash.

**Bug fixed:** First logo push failed with `iPixel raw message too large` because `ipixelSendRawMessage()` reused the PNG buffer limit. Logo window 0 is 12,303 bytes; chunk sending already worked — only the size check needed updating.

---

## End goal (achieved)

The standalone game loop works end to end:

1. ESP32 powers on
2. Logo GIF pushed directly on the iPixel (loops until first at-bat)
3. Animated attract loop plays on the round LCD
4. Players connect to Wi-Fi and tap Join Game (Home then Away)
5. The pitching and batting teams lock their choices for the half
6. ESP32 resolves the play via the weighted outcome lookup (with injected ball/strike chances)
7. iPixel shows the result animation or scroll text (walk/strikeout)
8. Scoreboard updates on slot 5
9. Play continues through three innings
10. After the bottom of the 3rd, **End Game** triggers the wrap-up banner, reset, and logo attract

No laptop, cloud service, or external server required during gameplay.

**Still open (optional v2 ideas):** See [details.md](details.md) — possible future improvements.

---

## iPixel diagnostic history

Normal gameplay uses `IPIXEL_DIAGNOSTIC_MODE 0`, `IPIXEL_WRITE_WITH_RESPONSE 1`, `IPIXEL_DIAG_HANDLE_SCAN 0`, and `IPIXEL_RAW_WRITE_HANDLE 0x0006`.

When BLE commands queue successfully but the display ignores them, this was the test order used during bring-up:

1. Set `IPIXEL_DIAGNOSTIC_MODE` to `1` and flash. Wi-Fi is disabled; firmware cycles slots 1, 4, 7, and 10 (strike) from `loop()`.
2. If mode 1 logs `rc=0` but the display ignores slots, set `IPIXEL_USE_AE_CHANNEL` to `1` and retest (switches from `fa02`/`fa03` to `ae01`/`ae02`).
3. If the channel switch still fails, compare `IPIXEL_WRITE_WITH_RESPONSE` `0` vs `1` in mode 1.
4. Set `IPIXEL_REQUIRE_SLOT_ACK` to `1` in mode 1. A timeout means the display is not accepting that channel/write mode.
5. Set `IPIXEL_DIAG_HANDLE_SCAN` to `1` in mode 1. Writes `show_slot` to handles `0x0005` through `0x000F`.
6. Set `IPIXEL_DIAGNOSTIC_MODE` to `2` — Wi-Fi on, same slot cycle, no phone input. Isolates Wi-Fi/BLE coexistence.
7. Return to mode `0` for normal two-phone gameplay.

**Notify channel:** The iPixel notify characteristic (`fa03`) has never produced a packet on the ESP32 connection, even for the device-info handshake. Image and slot ACKs are not waited on; the panel still receives and displays pushed content.
