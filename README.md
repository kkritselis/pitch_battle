<img src="./logo_readme.gif" alt="Pitch Battle logo" width="100%">

# Pitch Battle

Pitch Battle is a self-contained tabletop baseball game. Players connect their phones to a local Wi-Fi network, choose pitch and swing options, and the host resolves each at-bat. Results appear on the phones and on an external iPixel LED display.

The game needs no laptop, internet connection, or cloud services.

---

## Project History

This repo is a **rebuild** of an earlier Mac-based prototype.


|                   | Mac prototype (previous)     | This repo (current)                      |
| ----------------- | ---------------------------- | ---------------------------------------- |
| Host              | Node.js on a Mac             | ESP32-C3                                 |
| Web app           | Served from Node             | Embedded in firmware (`src/main.cpp`)    |
| Wi-Fi             | Mac hotspot or local network | ESP32 access point + captive portal      |
| Round LCD QR code | Not part of prototype        | GC9A01 display on ESP32                  |
| Game logic        | Node                         | `resolvePlay()` in firmware              |
| iPixel display    | Node BLE client (working)    | ESP32 BLE client with live scoreboard    |
| Animation uploads | Done via Mac tooling         | Slots already loaded on display hardware |


The Mac prototype proved out multiplayer flow, play resolution, and iPixel BLE control. This repo moves that stack onto the ESP32 so the game can run standalone. iPixel protocol details and slot assignments below come from the prototype work and are the reference for porting BLE into firmware.

---

## Current State

### Working in this repo

- **Wi-Fi access point** — SSID `PitchBattle`, password `pitchbattle`
- **Captive portal** — DNS redirect plus probe routes for Android and iOS
- **Round LCD** — QR code and connection info on boot (GC9A01, 240x240)
- **Web app** — Single Join Game screen with team-based pitch/bat controls (HTML/CSS/JS embedded in firmware)
- **Team assignment** — First phone to join is Home, second is Away; roles follow the inning half
- **REST API** — Join, pitch, swing, state, and reset endpoints
- **At-bat resolution** — `resolvePlay()` compares pitch height/speed to swing height/timing
- **Shared game state** — pitch/swing locks, inning, score, count, outs, and base runners
- **iPixel BLE client** — NimBLE scan/connect, raw ATT writes, logo on boot, result animations, and live scoreboard image push (`src/ipixel.cpp`)
- **Dynamic iPixel scoreboard** — firmware renders a 96x16 PNG from game state and pushes it after result animations
- **Next-pitch flow** — Web UI reset button calls `/api/reset` after a resolved play

### Not yet implemented

- **Full baseball rules** — Core count, base advancement, scoring, and inning changes exist; advanced plays are still simplified
- **Player persistence** — Team is claimed with a `localStorage` token (survives reload), but there is no account system or reconnection grace period

### Known rough edges

- `resultText` is not escaped when building JSON responses
- `platformio.ini` includes machine-specific serial ports; remove or change for your setup
- iPixel writes depend on raw ATT handle `0x0006` for this unit; use the diagnostic modes below when changing BLE code
- **iPixel image ACK never arrives on this link**: the panel does receive and
  display the pushed PNG, but no notify ACK (`0x05 ... code`) comes back, so the
  firmware currently times out and falls back to slot 9, overwriting the live
  image. The notify channel (`fa03`) has never produced a packet on the ESP32
  connection, even for the device-info handshake. Next step is to stop treating
  the missing ACK as a failure (and skip the slot-9 fallback) since the image
  displays regardless.
- **Static scoreboard test image is still enabled**: `IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG`
  is `1`, so the panel shows the fixed sample values from
  `include/scoreboard_test_png.h`, not live game state. Set it to `0` to push the
  real rendered scoreboard.
- **On-device PNG compression is unavailable**: the ESP32 ROM miniz compressor
  fails, so the renderer falls back to a valid but uncompressed PNG (~4.7 KB).
  That still fits within a single 12 KB window, so it transfers fine.



---

## Hardware

### ESP32-C3 DevKitM-1

- Hosts Wi-Fi, web server, game engine, and iPixel BLE client
- Drives the onboard round display for the QR code

### Round LCD (onboard)

- 1.28 inch 240x240 round IPS TFT
- GC9A01 driver
- Pin mapping in `include/config.h` — if the display stays blank, adjust pins there or set `ENABLE_LCD` to `0` to test the web server without the display

### iPixel display (external, from prototype)

- 96x16 flexible LED matrix
- Controlled over BLE
- Stores up to 10 animations internally; content survives power cycles
- Animations were uploaded during prototype work and remain on the device

### Phones

- Connect to the `PitchBattle` Wi-Fi network
- Open the captive portal or browse to `http://192.168.4.1`
- Tap Join Game: the first phone becomes Home, the second becomes Away
- Roles follow the inning half (top: Home pitches, Away bats; bottom: they swap)

---

## Architecture

### Today

```text
  Phone (Home team)            Phone (Away team)
         |                            |
         +------------+---------------+
                      |
                      v
              ESP32-C3 firmware
         +-------------------------+
         |  Wi-Fi AP               |
         |  Captive portal (DNS)   |
         |  Web server + UI        |
         |  Game state + resolve   |
         |  Round LCD (QR code)    |
         |  BLE client (iPixel)    |
         +-------------------------+
                      |
                      v
              iPixel LED display
```

### Target

```text
  Phone (Home team)            Phone (Away team)
         |                            |
         +------------+---------------+
                      |
                      v
              ESP32-C3 firmware
         +-------------------------+
         |  Wi-Fi AP + portal      |
         |  Web server + UI        |
         |  Game engine            |
         |  Round LCD (QR code)    |
         |  BLE client             |
         +-------------------------+
                      |
                      v
              iPixel LED display
         (result animations + scoreboard)
```

---

## Build and Run

Requires [PlatformIO](https://platformio.org/).

```bash
cd pitch_battle
pio run -t upload
pio device monitor
```

Serial monitor runs at 115200 baud.

After flashing:

1. Connect your phone to Wi-Fi network `PitchBattle`
2. Password: `pitchbattle`
3. The captive portal should open automatically; if not, go to `http://192.168.4.1`

---

## API


| Method | Path            | Description                                   |
| ------ | --------------- | --------------------------------------------- |
| `GET`  | `/`             | Game web UI                                   |
| `POST` | `/api/join`     | Claim a team (Home first, then Away)          |
| `GET`  | `/api/state`    | Current game state (JSON)                     |
| `POST` | `/api/pitch`    | Lock pitch selection (pitching team only)     |
| `POST` | `/api/swing`    | Lock swing selection (batting team only)      |
| `POST` | `/api/reset`    | Reset locks and start a new at-bat            |
| `POST` | `/api/new-game` | Reset the full scoreboard/game state          |


### Teams and roles

Each phone generates a random `token` (stored in `localStorage`) and posts it to
`/api/join`. The first token claims **Home**, the second claims **Away**, and any
further phone receives `{"team":"full"}`. A reload reuses the saved token, so a
phone reclaims its own team instead of taking the other slot.

Roles are derived from the team and the inning half, not chosen by the player:

| Half   | Home  | Away  |
| ------ | ----- | ----- |
| Top    | Pitch | Bat   |
| Bottom | Bat   | Pitch |

The server enforces this: `/api/pitch` is rejected with `403` unless the token
belongs to the pitching team, and `/api/swing` is rejected unless it belongs to
the batting team. The rejection body is `{"error":"..."}`.

### Join body

```json
{
  "token": "p-ab12cd34"
}
```

Response: `{"team":"home"}`, `{"team":"away"}`, or `{"team":"full"}`.

### Pitch body

```json
{
  "height": "high",
  "speed": "fast",
  "token": "p-ab12cd34"
}
```

`height`: `high`, `middle`, `low`  
`speed`: `fast`, `medium`, `slow`

### Swing body

```json
{
  "height": "middle",
  "timing": "medium",
  "token": "p-ab12cd34"
}
```

`height`: `high`, `middle`, `low`  
`timing`: `fast`, `medium`, `slow`

### State response

```json
{
  "pitchLocked": true,
  "swingLocked": false,
  "result": "",
  "image": "waiting",
  "inning": 1,
  "half": "top",
  "homeScore": 0,
  "awayScore": 0,
  "balls": 0,
  "strikes": 0,
  "outs": 0,
  "runnerFirst": false,
  "runnerSecond": false,
  "runnerThird": false
}
```

When both players lock, `result` contains the resolved outcome and `image` holds a category string (`homerun`, `double`, `single`, `foul`, `strike`, `flyout`, `waiting`) intended for iPixel slot selection. Scoreboard fields persist across at-bats until `/api/new-game` is called.

### Captive portal routes

These redirect or respond so phones detect the portal cleanly:

`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/canonical.html`, `/ncsi.txt`, `/fwlink`, `/success.txt`, `/favicon.ico`

---

## iPixel Reference (from Mac prototype)

The firmware now uses the stored-slot command path for animations and a compact image-transfer path for the dynamic scoreboard. The Mac tooling remains the reference for slot uploads.

### BLE characteristics


| Direction | UUID                                   |
| --------- | -------------------------------------- |
| Write     | `0000fa02-0000-1000-8000-00805f9b34fb` |
| Notify    | `0000fa03-0000-1000-8000-00805f9b34fb` |


### Transfer format (for uploading new content)

- 244-byte chunks
- 12 KB windows
- ACK-based transfers
- Live scoreboard uses `save_slot=0`; slot 9 remains the fallback static scoreboard

Image window framing (matches `pypixelcolor` `_build_send_plan` for PNG):

```text
[len-le16] [02 00 option] [size-le32] [crc32-le32] [00 save_slot] [png-bytes...]
```

- `len-le16` is the total message length including the 2-byte length field itself.
- The header after the length prefix is exactly **13 bytes** (`02 00 option` +
  4-byte size + 4-byte CRC32 + `00 save_slot`). Getting this count wrong corrupts
  the length prefix and truncates the payload, so the CRC never matches and the
  device silently refuses to ACK. This was the root cause of the long image-push
  failure; the writes all reported success while the window was rejected.
- `size` and `crc32` are computed over the raw PNG bytes, little-endian.
- `option` is `0x00` for the first window, `0x02` for subsequent windows.

### Slot display command

```text
07 00 08 80 01 00 <slot>
```

Send to the write characteristic to play a stored animation.

### Animation slot assignments


| Slot | Content    |
| ---- | ---------- |
| 1    | Homerun    |
| 2    | Triple     |
| 3    | Double     |
| 4    | Single     |
| 5    | Walk       |
| 6    | Ball       |
| 7    | Foul       |
| 8    | Flyout     |
| 9    | Scoreboard fallback |
| 10   | Logo       |


Slots persist across power cycles on the iPixel hardware.

### iPixel configuration

In `include/config.h`:

- `ENABLE_IPIXEL` — set to `0` to disable BLE and run Wi-Fi/LCD only
- `IPIXEL_DEVICE_PREFIX` — scan filter, default `LED_BLE_`
- `IPIXEL_ALT_PREFIX` — optional secondary name filter; leave empty unless needed
- `IPIXEL_MAC` — optional fixed MAC address; leave empty to scan by name
- `IPIXEL_SCAN_SECONDS` — boot scan timeout
- `IPIXEL_SLOT_*` — animation slot numbers
- `IPIXEL_DIAGNOSTIC_MODE` — troubleshooting mode:
  - `0` normal game
  - `1` BLE-only slot cycling, with Wi-Fi disabled
  - `2` Wi-Fi-on idle slot cycling, without phone input
- `IPIXEL_WRITE_WITH_RESPONSE` — experiment switch for comparing the ESP32
write path against the Mac client (`0` is the current stable default)
- `IPIXEL_USE_AE_CHANNEL` — experiment switch for the alternate vendor
`ae01`/`ae02` channel exposed by this iPixel (`0` uses documented
`fa02`/`fa03`)
- `IPIXEL_REQUIRE_SLOT_ACK` — experiment switch that waits for the notify ACK
after `show_slot`, matching the behavior expected by `pypixelcolor`
- `IPIXEL_DIAG_HANDLE_SCAN` — diagnostic-only raw ATT handle scan for cases
where UUID writes return success but the display ignores commands
- `IPIXEL_RAW_WRITE_HANDLE` — if nonzero, send commands to this raw ATT handle
instead of NimBLE's UUID-derived handle. Current diagnostics show handle
`0x0006` accepts response writes on this unit.
- `IPIXEL_IMAGE_RAW_WRITE_HANDLE` — raw ATT handle tried first for image upload
frames. `0` uses the characteristic-derived handle first, then falls back to
`IPIXEL_RAW_WRITE_HANDLE`.
- `IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG` — diagnostic flag. When `1`, the
firmware pushes a known-good Pillow-compressed PNG from
`include/scoreboard_test_png.h` instead of the live render, isolating transport
problems from runtime PNG generation. Set to `0` for live game state.
- `IPIXEL_SCOREBOARD_RETURN_MS` — how long a result animation stays on the
iPixel before the firmware pushes the live scoreboard image

**Before testing:** Close the iPixel Color phone app. BLE allows only one connection at a time.

**Power-on sequence:**

1. Turn on the iPixel display and wait for it to finish booting
2. Close the iPixel Color phone app
3. Power on or flash the ESP32 (power cycle works; there is a REST button on the back of the board/case if you need it)
4. The ESP32 tries BLE for about 12 seconds before starting Wi-Fi, then keeps retrying every 30 seconds until the logo is shown
5. Logo should appear once on the iPixel

**Reading the iPixel screen:**


| What you see                     | What it means                                                                                                    |
| -------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| Blinking blue link icon          | iPixel is advertising and waiting for a BLE connection. Good time for ESP32 to connect.                          |
| Animations cycling, no link icon | iPixel is playing stored content on its own. It is usually **not** advertising BLE, so the ESP32 cannot connect. |
| Logo holding steady              | ESP32 connected and sent slot 10 successfully                                                                    |


If the iPixel is stuck cycling animations, reset it from your Mac:

```bash
python3 -m pypixelcolor --scan
python3 -m pypixelcolor -a <your-device-id> -c show_slot 10
```

Use the address from `--scan` (looks like `LED_BLE_<id>`). Then power-cycle the ESP32 while the iPixel is idle.

**Best test sequence:**

1. Power off the iPixel
2. Run the Mac `show_slot 10` command above (or power iPixel on briefly and run it while the link icon appears)
3. Power on the ESP32
4. Within 12 seconds, power on the iPixel while it is advertising (link icon visible)

Your display should advertise as `LED_BLE_<id>`. Other BLE devices nearby are ignored by default.

**Serial monitor:** The ESP32-C3 uses USB serial. After flashing, the port is busy if a monitor is already open.

```bash
# Upload then immediately open monitor (recommended)
pio run -t upload -t monitor
```

If upload says "port is busy", press `Ctrl+C` in the monitor terminal first, then upload again.

You should see `Pitch Battle booting...` within a couple seconds of reset. If you only see `ESP-ROM` lines and nothing else, power-cycle the ESP32 while the monitor is already open.

Watch the round LCD line `BLE devices: N` during boot. That count is the key diagnostic for whether the ESP32 radio is seeing anything.

---

## Roadmap

### Phase 1 — iPixel BLE on ESP32 (complete)

- [x] Add NimBLE-Arduino to `platformio.ini`
- [x] Scan for and connect to the iPixel on boot
- [x] Discover the FA02 write characteristic
- [x] Implement `showIPixelResult()` using the slot display command
- [x] Show slot 10 (logo) at startup

**Done:** Logo appears on the iPixel automatically after ESP32 boot, with no Mac involved.

Key findings from bring-up:

- The display ignores NimBLE's UUID-derived write handle for `fa02` (`0x0009`)
even though queued writes report success. Raw ATT handle `0x0006` is the
working command handle for this unit.
- Commands are sent with GATT write-with-response to handle `0x0006`, matching
the Mac transport behavior closely enough for stored-slot control.
- `Serial` is routed to USB via `ARDUINO_USB_MODE=1` and
`ARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` for boot logging.
- The target unit (`LED_BLE_2D84B28A`) is pinned by address in
`IPIXEL_MAC` (`config.h`) for direct connect; clear it to scan by name.
- The current board has a small internal antenna already installed; do not
assume the antenna is missing. Treat iPixel issues as protocol/coexistence
problems until the diagnostic modes below isolate the failure.

### Phase 2 — Result animations (complete)

Wire play resolution to the correct iPixel slots.

- [x] `resolvePlay()` returns a structured `PlayResult` (text + image token),
  ```
  replacing fragile string matching
  ```
- [x] Map outcomes to slots: homerun, double, single, foul, strike, flyout
- [x] Trigger the animation immediately after `resolvePlay()` completes
- [x] Add a "Next pitch" button in the web UI that calls `/api/reset`
- [x] Parse JSON responses on the client so the UI no longer flashes raw JSON;
  ```
  show the result and a clean status line, and hide controls once resolved
  ```
- [x] iPixel reliably switches animation per play

**Status:** The phone/game side is fully working — pitch/swing lock, resolution,
result text on both phones, and "Next pitch" reset. Resolved plays now queue
iPixel work for the main loop instead of calling BLE from the HTTP request
handler. The iPixel transport is fixed by using raw ATT handle `0x0006` with
write-with-response.

Current iPixel troubleshooting status:

- The board already has a small internal antenna installed, so the previous
"missing antenna" conclusion was too narrow.
- `show_slot` bytes match the official `pypixelcolor` command exactly:
`0x07 0x00 0x08 0x80 0x01 0x00 <slot>`.
- `pypixelcolor` sends through Bleak with `response=True`. On ESP32/NimBLE,
write-with-response works when sent directly to raw handle `0x0006`; the
UUID-derived handle `0x0009` fails.
- Result commands are now loop-driven and non-blocking. During each result
burst, the firmware temporarily prefers BLE coexistence and restores balance
after the burst.

Notes:

- `show_slot` (`0x07 0x00 0x08 0x80 0x01 0x00 <slot>`) matches `pypixelcolor`
exactly. If the requested slot is empty, the device falls back to cycling
through populated slots.
- Diagnostic mode 1 confirmed slots 1, 4, 7, and 10 switch correctly with
`IPIXEL_RAW_WRITE_HANDLE 0x0006`.
- Do not call blocking BLE reads/writes from the web-server request handler
task; a diagnostic `readValue()` there caused a load-access-fault crash.
- `triple` and `walk` slots are mapped in `showIPixelResult()` but not yet
produced by `resolvePlay()`; they are reserved for Phase 5 rules.

Diagnostic test order:

Normal gameplay should use `IPIXEL_DIAGNOSTIC_MODE 0`,
`IPIXEL_WRITE_WITH_RESPONSE 1`, `IPIXEL_DIAG_HANDLE_SCAN 0`, and
`IPIXEL_RAW_WRITE_HANDLE 0x0006`.

1. Set `IPIXEL_DIAGNOSTIC_MODE` to `1` and flash. Wi-Fi is disabled and the
  firmware cycles slots 1, 4, 7, and 10 from `loop()`. If this fails, focus on
   protocol/slot contents.
2. If mode 1 logs `rc=0` but the display ignores the slots, set
  `IPIXEL_USE_AE_CHANNEL` to `1` and retest mode 1. This switches from
   documented `fa02`/`fa03` to the alternate `ae01`/`ae02` vendor channel shown
   in the GATT table.
3. If the channel switch still fails, set `IPIXEL_WRITE_WITH_RESPONSE` to `1`
  while staying in mode 1 and compare serial logs. Restore it to `0` afterward
   unless it proves more reliable.
4. Set `IPIXEL_REQUIRE_SLOT_ACK` to `1` while staying in mode 1. If the log
  shows `iPixel command ack timeout`, the display is not accepting that
   channel/write mode even though NimBLE queued the packet.
5. Set `IPIXEL_DIAG_HANDLE_SCAN` to `1` while staying in mode 1. The firmware
  writes `show_slot` directly to handles `0x0005` through `0x000F`; if any
   handle produces an ACK or visible slot change, pin that handle in the iPixel
   transport.
6. Set `IPIXEL_DIAGNOSTIC_MODE` to `2` and flash. Wi-Fi is active but phones are
  not needed; the same slot cycle runs outside HTTP handlers. If mode 1 passes
   but mode 2 fails, focus on Wi-Fi/BLE coexistence.
7. Return `IPIXEL_DIAGNOSTIC_MODE` to `0` and test normal two-phone gameplay. If
  modes 1 and 2 pass but gameplay fails, focus on request timing or game-state
   dispatch.

### Phase 3 — Scoreboard state (complete)

Track game progression between at-bats.

- [x] Inning (top/bottom)
- [x] Home and away score
- [x] Balls, strikes, outs
- [x] Base runners

Maintain state on the ESP32 and expose it through `/api/state`.

**Done:** Count, outs, score, inning half, and base runners advance across
multiple pitch/swing cycles. `/api/reset` starts the next pitch while preserving
the scoreboard; `/api/new-game` resets the full game state.

Notes:

- Singles advance runners one base, doubles two bases, and home runs score all
runners plus the batter.
- Fouls add a strike only when the batter has fewer than two strikes.
- Three strikes or any out result records an out; three outs clear the bases and
advance the half-inning.

### Phase 4 — iPixel scoreboard return flow (in progress)

Show live game state on the LED matrix.

- [x] Flow: result animation plays, then scoreboard returns
- [x] Use stored scoreboard slot 9 as the fallback return screen
- [x] Configurable delay via `IPIXEL_SCOREBOARD_RETURN_MS`
- [x] Render live score/count/base runner pixels into the iPixel scoreboard
- [x] Push generated scoreboard PNG over BLE with `save_slot=0` (image displays)
- [ ] Treat the missing notify ACK as success and skip the slot-9 fallback
- [ ] Disable the static test PNG and verify live game state renders correctly

**Status:** The image-push path now reaches the panel and the scoreboard image
displays. The 13-byte window header fix was the breakthrough — earlier the
length prefix was off by 2 bytes, truncating the payload and silently failing the
CRC. Two things remain before the live scoreboard is correct:

1. The device never returns a notify ACK on this link, so the firmware times out
   and falls back to slot 9, overwriting the freshly pushed image. The fix is to
   stop gating on the ACK since the image clearly displays without it.
2. `IPIXEL_USE_STATIC_SCOREBOARD_TEST_PNG` is still `1`, so the panel shows fixed
   sample values rather than live `GameState`. Set it to `0` to push the real
   render.

The template contract is:

- Balls, strikes, and outs render as single 3x5 digits on the left edge.
- Occupied first, second, and third base boxes render red in the center diamond.
- The right-side grid renders visitor and home rows with inning 1-3 runs and a
  total in the far-right box.

### Phase 5 — Full baseball rules

Expand beyond single at-bat outcomes.

- Walks and balls
- Double plays, sacrifice flies, and similar situational rules

**Done when:** A full game can be played start to finish with correct scoring and inning flow.

---

## End Goal

1. ESP32 powers on
2. Logo appears on the iPixel
3. QR code appears on the round LCD
4. Players scan, join Wi-Fi, and tap Join Game (Home then Away)
5. The pitching and batting teams lock their choices for the half
6. ESP32 resolves the play
7. iPixel shows the result animation
8. Scoreboard updates
9. Play continues through full innings

No laptop, cloud service, or external server required.

---

## Project Layout

```text
pitch_battle/
  assets/ipixel/      iPixel source artwork and fallback slot assets
  include/config.h    Wi-Fi credentials, TFT pins, iPixel settings
  include/ipixel.h    iPixel BLE API
  include/scoreboard.h  Dynamic scoreboard render API
  src/main.cpp        Wi-Fi, portal, web UI, game logic
  src/ipixel.cpp      iPixel BLE client
  src/scoreboard.cpp  96x16 scoreboard renderer and PNG encoder
  setup/storeImages.py  Mac helper for uploading fallback slot assets
  platformio.ini      Board and library dependencies
  LICENSE             MIT
```

