# Pitch Battle ESP32 QR Host

This is the first ESP32 version of Pitch Battle.

It does:

- Creates a Wi-Fi access point named `PitchBattle`
- Hosts a local web app at `http://192.168.4.1`
- Displays a QR code on the ESP32 round LCD
- Provides first-pass pitcher/hitter phone screens

It does not yet:

- Resolve game logic
- Control the iPixel display
- Store player state across phones

## Hardware Assumption

This project assumes:

- ESP32-C3
- 1.28 inch 240x240 round IPS TFT
- GC9A01 display driver

If the display stays blank, the likely issue is the TFT pin mapping in:

```cpp
include/config.h
```

## Build with PlatformIO

```bash
cd pitch_battle_esp32_qr
pio run -t upload
pio device monitor
```

## Wi-Fi

After flashing:

1. Connect your phone to Wi-Fi network `PitchBattle`
2. Password: `pitchbattle`
3. Open `http://192.168.4.1`

# Pitch Battle ESP32 Project

## Overview

Pitch Battle is a self-contained multiplayer baseball game built around an ESP32-C3 and a 96x16 iPixel LED display.

The ESP32 acts as:

- Wi-Fi Access Point
- Captive Portal
- Web Server
- Game Engine
- BLE Client for the iPixel Display

Players connect directly to the ESP32 using their phones, select pitching and batting options, and the ESP32 resolves the at-bat. The result is displayed both on the players' phones and on the external iPixel display.

The long-term goal is a tabletop baseball game that requires no external computer, internet connection, or cloud services.

---

# Hardware

## ESP32

- ESP32-C3 DevKitM-1
- Hosts the game
- Displays QR code on onboard round display
- Creates local Wi-Fi network

## iPixel Display

- 96x16 flexible LED display
- Controlled via BLE
- Stores up to 10 animations internally
- Supports static images and animated GIFs
- Memory survives power cycles

## Phones

- Connect to ESP32 Wi-Fi
- Access captive portal web application
- Used as Pitcher and Hitter interfaces

---

# Current Architecture

```text
          Phone 1 (Pitcher)
                  |
                  |
                  v
          ESP32 Web Server
                  ^
                  |
                  |
          Phone 2 (Hitter)

                  |
                  |
                  v

         Game Resolution Engine

                  |
                  |
                  v

            BLE Connection

                  |
                  |
                  v

            iPixel Display
```

---

# Accomplishments

## Wi-Fi Access Point

Successfully configured ESP32 as a standalone access point.

SSID:

```text
PitchBattle
```

Users connect directly without internet access.

---

## Captive Portal

Implemented captive portal support.

Verified working on Android.

Users:

1. Scan QR code
2. Join Wi-Fi
3. Portal opens automatically

Additional routes added to eliminate portal errors:

```text
/generate_204
/hotspot-detect.html
/connecttest.txt
/canonical.html
/ncsi.txt
/fwlink
/success.txt
/favicon.ico
```

---

## Web Application

Implemented browser-based game UI.

Supports:

- Join as Pitcher
- Join as Hitter
- Lock Pitch
- Lock Swing
- Game State Updates

---

## Shared Game State

Implemented:

```cpp
pitchLocked
swingLocked
pitchValue
swingValue
resultText
```

State is maintained on the ESP32.

---

## Resolution Logic

Implemented:

```cpp
resolvePlay()
```

Current logic evaluates:

- Pitch location
- Pitch speed
- Swing location
- Swing timing

Generates baseball outcomes such as:

- Home Run
- Double
- Single
- Strike
- Fly Out
- Ground Out
- Foul Ball

---

## Reset System

Implemented reset endpoint.

Resets:

```cpp
pitchLocked
swingLocked
pitchValue
swingValue
resultText
```

Returns game to:

```text
Waiting for lock
```

---

## iPixel Reverse Engineering

Successfully determined:

### BLE Characteristics

Write:

```text
0000fa02-0000-1000-8000-00805f9b34fb
```

Notify:

```text
0000fa03-0000-1000-8000-00805f9b34fb
```

### Transfer System

- 244-byte chunks
- 12 KB windows
- ACK-based transfers

### Slot Display Command

Discovered slot display command:

```cpp
07 00 08 80 01 00 <slot>
```

---

## Slot Storage Discovery

Confirmed that iPixel content storage is persistent.

Procedure:

1. Uploaded animations
2. Powered off display
3. Powered display back on

Result:

- Animations remained stored
- Display replayed stored content

Conclusion:

```text
Slots survive power loss.
```

---

# Animation Slot Assignments

```text
Slot 1  Homerun
Slot 2  Triple
Slot 3  Double
Slot 4  Single
Slot 5  Walk
Slot 6  Ball
Slot 7  Foul
Slot 8  Flyout
Slot 9  Scoreboard
Slot 10 Logo
```

---

# Current Project Status

## Complete

### ESP32

- AP Mode
- Captive Portal
- QR Code
- Web Server
- Game Logic
- State Management

### iPixel

- BLE Protocol Identified
- Slots Uploaded
- Persistence Verified

### Phones

- Multiplayer Connection
- Pitch Selection
- Swing Selection
- Resolution Display

---

# Immediate Next Step

## Connect ESP32 to iPixel

Goal:

```text
ESP32 Boot
    ↓
Connect BLE
    ↓
Show Slot 10
    ↓
Display Logo
```

Tasks:

- Add NimBLE-Arduino
- Scan for iPixel
- Connect to display
- Discover FA02 characteristic
- Send slot display command

Success Criteria:

Logo appears automatically at startup.

---

# Phase 2

## Result Animation Playback

After game resolution:

```cpp
showSlot(1); // Homerun
showSlot(2); // Triple
showSlot(3); // Double
showSlot(4); // Single
showSlot(5); // Walk
showSlot(6); // Ball
showSlot(7); // Foul
showSlot(8); // Flyout
```

Animation should play immediately after each pitch result.

---

# Phase 3

## Scoreboard State Engine

Track:

```text
Inning
Home Score
Away Score
Balls
Strikes
Outs
Base Runners
```

Maintain game progression between at-bats.

---

# Phase 4

## Dynamic Scoreboard Rendering

Generate scoreboard image on ESP32.

Display:

```text
Score
Balls
Strikes
Outs
Bases Occupied
Inning
```

Push updated image to iPixel.

Potential workflow:

```text
Result Animation
       ↓
Animation Ends
       ↓
Scoreboard Image
       ↓
Wait For Next Pitch
```

---

# Phase 5

## Full Baseball Rules

Expand resolution system to include:

- Base advancement
- Runs scored
- Double plays
- Sacrifice flies
- Walks
- Strikeouts
- Inning changes

---

# Final Goal

A completely self-contained tabletop baseball game where:

1. ESP32 powers on.
2. Logo animation appears.
3. Players scan QR code.
4. Phones join game.
5. Pitch and swing selections are made.
6. ESP32 resolves play.
7. iPixel displays result animation.
8. Scoreboard updates.
9. Game continues through full baseball innings.

No laptop, cloud service, or external server required.