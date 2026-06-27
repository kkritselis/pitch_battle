<img src="./logo_readme.gif" alt="Pitch Battle logo" width="100%">

# Pitch Battle

Pitch Battle is a self-contained tabletop baseball game. Two phones connect to an ESP32 over local Wi-Fi, pick pitch and swing options in secret, and the host resolves each at-bat. Results show on the phones and on an iPixel LED scoreboard — no laptop, internet, or cloud service required during play.

MIT licensed. See [LICENSE](LICENSE).

---

## How it works

1. Power on the ESP32 and iPixel display.
2. Two players join Wi-Fi **PitchBattle** on their phones (first join = Home, second = Away).
3. Each half-inning, one player pitches and the other bats. Both choose **height** and **speed/timing**, then lock in.
4. The ESP32 resolves the play, updates the scoreboard, and plays an animation on the iPixel.
5. Tap **Next Pitch** and keep playing through **3 innings**.
6. After the final out, tap **End Game** for the score banner and a fresh attract screen.

Top of the inning: Home pitches, Away bats. Bottom: roles swap. Base runners on the phone and iPixel scoreboard are **blue** when Away is batting and **red** when Home is batting.

---

## Hardware

| Part | Role |
| ---- | ---- |
| **ESP32-C3 DevKitM-1** | Wi-Fi host, game engine, web UI, round LCD |
| **Round LCD** (onboard GC9A01) | Attract animation on the dev kit |
| **iPixel** 96×16 LED matrix (BLE) | Animations, scroll text, live scoreboard |
| **2 phones** | One per team |

You need to upload GIF assets to the iPixel once from a Mac (see [details.md](details.md)). The logo animation is embedded in the ESP32 firmware and pushed directly at boot.

---

## Install and run

### Requirements

- [PlatformIO](https://platformio.org/)
- USB cable to the ESP32-C3
- Python 3 (for one-time iPixel GIF upload and optional asset edits)

### Flash the ESP32

```bash
git clone <your-repo-url>
cd pitch_battle
```

Edit `upload_port` in `platformio.ini` if your serial device path differs, then:

```bash
pio run -t upload
```

### Upload iPixel animations (one time)

Power off the ESP32. Close the iPixel Color phone app. With the iPixel on and advertising:

```bash
pip install pypixelcolor bleak
python3 setup/storeImages.py
```

### Play

1. Power on the iPixel, then the ESP32.
2. On each phone: join Wi-Fi **PitchBattle** / password **pitchbattle**
3. Open the captive portal, or go to **http://192.168.4.1**
4. Tap **Join Game** (Home first, then Away).

---

## Documentation

| Document | Contents |
| -------- | -------- |
| [details.md](details.md) | API, iPixel slots, config flags, project layout, developer notes |
| [project_history.md](project_history.md) | How the project evolved and bring-up notes |

---

## License

[MIT](LICENSE) — Copyright (c) 2026 Keith Kritselis
