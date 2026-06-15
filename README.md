# Pitch Battle

Pitch Battle is a self-contained tabletop baseball game. Players connect their phones to a local Wi-Fi network, choose pitch and swing options, and the host resolves each at-bat. Results appear on the phones and, eventually, on an external iPixel LED display.

The long-term goal is a game that needs no laptop, internet connection, or cloud services.

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
- **iPixel BLE client** — NimBLE scan/connect, FA02 writes, logo on boot (`src/ipixel.cpp`)

### Not yet implemented

- **Result animations on iPixel during play** — Slot mapping exists; resolution triggers slots but needs hardware verification
- **Next-pitch flow** — `/api/reset` exists but the web UI has no reset button; state stays locked after resolution
- **Scoreboard** — No inning, count, outs, bases, or score tracking
- **Full baseball rules** — Single at-bat outcomes only; no base advancement or game progression
- **Player persistence** — No session or role assignment beyond what each phone keeps in memory

### Known rough edges

- Pitch/swing handlers return JSON; the web UI briefly shows raw JSON in the status line before polling updates it
- `resultText` is not escaped when building JSON responses
- `platformio.ini` includes machine-specific serial ports; remove or change for your setup

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


| Method | Path         | Description                        |
| ------ | ------------ | ---------------------------------- |
| `GET`  | `/`          | Game web UI                        |
| `GET`  | `/api/state` | Current game state (JSON)          |
| `POST` | `/api/pitch` | Lock pitcher selection (JSON body) |
| `POST` | `/api/swing` | Lock hitter selection (JSON body)  |
| `POST` | `/api/reset` | Reset locks and start a new at-bat |


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
  "image": "waiting"
}
```

When both players lock, `result` contains the resolved outcome and `image` holds a category string (`homerun`, `strike`, `single`, `out`, `contact`, `waiting`) intended for iPixel slot selection.

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

**Before testing:** Close the iPixel Color phone app. BLE allows only one connection at a time.

**Power-on sequence:**

1. Turn on the iPixel display and wait for it to finish booting
2. Close the iPixel Color phone app
3. Power on or flash the ESP32 (power cycle works; there is a REST button on the back of the board/case if you need it)
4. The ESP32 tries BLE for about 12 seconds before starting Wi-Fi, then keeps retrying every 30 seconds until the logo is shown
5. Logo should appear once on the iPixel

**Reading the iPixel screen:**

| What you see | What it means |
|---|---|
| Blinking blue link icon | iPixel is advertising and waiting for a BLE connection. Good time for ESP32 to connect. |
| Animations cycling, no link icon | iPixel is playing stored content on its own. It is usually **not** advertising BLE, so the ESP32 cannot connect. |
| Logo holding steady | ESP32 connected and sent slot 10 successfully |

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

### Phase 1 — iPixel BLE on ESP32 (implemented, needs hardware test)

- [x] Add NimBLE-Arduino to `platformio.ini`
- [x] Scan for and connect to the iPixel on boot
- [x] Discover the FA02 write characteristic
- [x] Implement `showIPixelResult()` using the slot display command
- [x] Show slot 10 (logo) at startup

**Done when:** Logo appears on the iPixel automatically after ESP32 boot, with no Mac involved.

### Phase 2 — Result animations

Wire play resolution to the correct iPixel slots.

- Map `currentImage` and `resultText` outcomes to slots 1–8
- Trigger the animation immediately after `resolvePlay()` completes
- Add a "Next pitch" button in the web UI that calls `/api/reset`
- Fix pitch/swing status responses so the UI does not flash raw JSON

**Done when:** A full pitch-and-swing cycle updates both phones and the iPixel without manual reset via serial or curl.

### Phase 3 — Scoreboard state

Track game progression between at-bats.

- Inning (top/bottom)
- Home and away score
- Balls, strikes, outs
- Base runners

Maintain state on the ESP32 and expose it through `/api/state`.

**Done when:** Count and score advance correctly across multiple at-bats.

### Phase 4 — Dynamic scoreboard on iPixel

Show live game state on the LED matrix.

- Render a scoreboard image on the ESP32 (or update slot 9 content)
- Push updates after each at-bat
- Flow: result animation plays, then scoreboard returns

**Done when:** The iPixel alternates between result animations and an accurate scoreboard.

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

