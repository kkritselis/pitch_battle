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
| iPixel display    | Node BLE client (working)    | Stub only (`showIPixelResult()`)         |
| Animation uploads | Done via Mac tooling         | Slots already loaded on display hardware |


The Mac prototype proved out multiplayer flow, play resolution, and iPixel BLE control. This repo moves that stack onto the ESP32 so the game can run standalone. iPixel protocol details and slot assignments below come from the prototype work and are the reference for porting BLE into firmware.

---

## Current State

### Working in this repo

- **Wi-Fi access point** — SSID `PitchBattle`, password `pitchbattle`
- **Captive portal** — DNS redirect plus probe routes for Android and iOS
- **Round LCD** — QR code and connection info on boot (GC9A01, 240x240)
- **Web app** — Pitcher and hitter phone screens (HTML/CSS/JS embedded in firmware)
- **REST API** — Pitch, swing, state, and reset endpoints
- **At-bat resolution** — `resolvePlay()` compares pitch height/speed to swing height/timing
- **Shared game state** — `pitchLocked`, `swingLocked`, `pitchValue`, `swingValue`, `resultText`
- **iPixel BLE client** — NimBLE scan/connect, FA02 writes, logo on boot, result command queue (`src/ipixel.cpp`)
- **Next-pitch flow** — Web UI reset button calls `/api/reset` after a resolved play

### Not yet implemented

- **Result animations on iPixel during play** — Slot mapping exists and resolution queues slots; reliability is under active troubleshooting
- **Scoreboard** — No inning, count, outs, bases, or score tracking
- **Full baseball rules** — Single at-bat outcomes only; no base advancement or game progression
- **Player persistence** — No session or role assignment beyond what each phone keeps in memory

### Known rough edges

- `resultText` is not escaped when building JSON responses
- `platformio.ini` includes machine-specific serial ports; remove or change for your setup
- iPixel result writes are BLE-write-without-response commands; use the diagnostic modes below when changing that code

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
- One phone as pitcher, one as hitter

---

## Architecture

### Today

```text
  Phone (Pitcher)              Phone (Hitter)
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
  Phone (Pitcher)              Phone (Hitter)
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


| Method | Path            | Description                          |
| ------ | --------------- | ------------------------------------ |
| `GET`  | `/`             | Game web UI                          |
| `GET`  | `/api/state`    | Current game state (JSON)            |
| `POST` | `/api/pitch`    | Lock pitcher selection (JSON body)   |
| `POST` | `/api/swing`    | Lock hitter selection (JSON body)    |
| `POST` | `/api/reset`    | Reset locks and start a new at-bat   |
| `POST` | `/api/new-game` | Reset the full scoreboard/game state |


### Pitch body

```json
{
  "height": "high",
  "speed": "fast"
}
```

`height`: `high`, `middle`, `low`  
`speed`: `fast`, `medium`, `slow`

### Swing body

```json
{
  "height": "middle",
  "timing": "medium"
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

This firmware does not use these yet. They document what was learned on the Mac and what needs to be ported.

### BLE characteristics


| Direction | UUID                                   |
| --------- | -------------------------------------- |
| Write     | `0000fa02-0000-1000-8000-00805f9b34fb` |
| Notify    | `0000fa03-0000-1000-8000-00805f9b34fb` |


### Transfer format (for uploading new content)

- 244-byte chunks
- 12 KB windows
- ACK-based transfers

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
| 9    | Scoreboard |
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
- `IPIXEL_SCOREBOARD_RETURN_MS` — how long a result animation stays on the
iPixel before returning to the stored scoreboard slot

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

### Phase 4 — iPixel scoreboard return flow (complete)

Show live game state on the LED matrix.

- [x] Flow: result animation plays, then scoreboard returns
- [x] Use stored scoreboard slot 9 as the iPixel return screen
- [x] Configurable delay via `IPIXEL_SCOREBOARD_RETURN_MS`
- [ ] Render live score/count/base runner pixels into the iPixel scoreboard

**Done:** The iPixel alternates between result animations and the stored
scoreboard slot. Live dynamic scoreboard rendering is left for a later graphics
pass; the phone UI remains the source of truth for exact count, score, and base
state.

### Phase 5 — Full baseball rules

Expand beyond single at-bat outcomes.

- Base advancement and runs scored
- Walks, strikeouts, and inning changes
- Double plays, sacrifice flies, and similar situational rules

**Done when:** A full game can be played start to finish with correct scoring and inning flow.

---

## End Goal

1. ESP32 powers on
2. Logo appears on the iPixel
3. QR code appears on the round LCD
4. Players scan, join Wi-Fi, and open the game
5. Pitcher and hitter lock their choices
6. ESP32 resolves the play
7. iPixel shows the result animation
8. Scoreboard updates
9. Play continues through full innings

No laptop, cloud service, or external server required.

---

## Project Layout

```text
pitch_battle/
  include/config.h    Wi-Fi credentials, TFT pins, iPixel settings
  include/ipixel.h    iPixel BLE API
  src/main.cpp        Wi-Fi, portal, web UI, game logic
  src/ipixel.cpp      iPixel BLE client
  platformio.ini      Board and library dependencies
  LICENSE             MIT
```

