import asyncio
from pathlib import Path
from pypixelcolor import AsyncClient

ADDRESS = "80CE82D9-A461-A4D3-85E2-40A6D737DDEA"
PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = PROJECT_ROOT / "assets" / "ipixel"

FILES = [
    (1, "homerun.gif"),
    (2, "triple.gif"),
    (3, "double.gif"),
    (4, "single.gif"),
    (5, "walk.gif"),
    (6, "ball.gif"),
    (7, "foul.gif"),
    (8, "flyout.gif"),
    (9, "scoreboard_template.png"),
    (10, "logo.gif"),
]

async def main():
    async with AsyncClient(ADDRESS) as device:
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