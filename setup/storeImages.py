import asyncio
from pathlib import Path

from bleak import BleakScanner
from pypixelcolor import AsyncClient

# The iPixel advertises as LED_BLE_<id>. We scan for it by name because the
# CoreBluetooth address on macOS is per-host and can change between sessions.
NAME_PREFIX = "LED_BLE_"
# Optional override: set to a known address to skip scanning. Leave as None to
# always discover by name.
ADDRESS = None
SCAN_SECONDS = 8.0

ASSET_DIR = Path(__file__).resolve().parent

# Slot 5 is reserved for the live scoreboard pushed by ESP32 firmware.
FILES = [
    (1, "homerun.gif"),
    (2, "triple.gif"),
    (3, "double.gif"),
    (4, "single.gif"),
    (6, "ball.gif"),
    (7, "foul.gif"),
    (8, "fly_out.gif"),
    (9, "ground_out.gif"),
    (10, "logo.gif"),
]

async def resolve_address():
    if ADDRESS:
        return ADDRESS

    print(f"Scanning for a device named '{NAME_PREFIX}*' ({SCAN_SECONDS:.0f}s)...")
    devices = await BleakScanner.discover(timeout=SCAN_SECONDS)
    for d in devices:
        if d.name and d.name.startswith(NAME_PREFIX):
            print(f"Found {d.name} at {d.address}")
            return d.address

    found = ", ".join(sorted(d.name for d in devices if d.name)) or "(none)"
    raise SystemExit(
        f"No '{NAME_PREFIX}*' device found. Make sure the iPixel is powered on, "
        "the ESP32 is off (only one BLE connection is allowed at a time), and "
        f"the iPixel Color app is closed.\nVisible devices: {found}"
    )

async def main():
    address = await resolve_address()

    async with AsyncClient(address) as device:
        for slot, filename in FILES:
            image_path = ASSET_DIR / filename
            if not image_path.exists():
                print(f"Skipping missing asset {image_path}")
                continue

            print(f"Uploading {image_path} to slot {slot}")
            await device.send_image(str(image_path), save_slot=slot)

            print(f"Showing slot {slot}")
            await device.show_slot(slot)

            await asyncio.sleep(1)

asyncio.run(main())

