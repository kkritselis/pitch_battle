import asyncio
from pypixelcolor import AsyncClient

ADDRESS = "80CE82D9-A461-A4D3-85E2-40A6D737DDEA"

FILES = [
    (1, "homerun.gif"),
    (2, "triple.gif"),
    (3, "double.gif"),
    (4, "single.gif"),
    (5, "walk.gif"),
    (6, "ball.gif"),
    (7, "foul.gif"),
    (8, "flyout.gif"),
    (9, "score_board.png"),
    (10, "logo.gif"),
]

async def main():
    async with AsyncClient(ADDRESS) as device:
        for slot, filename in FILES:
            print(f"Uploading {filename} to slot {slot}")
            await device.send_image(filename, save_slot=slot)

            print(f"Showing slot {slot}")
            await device.show_slot(slot)

            await asyncio.sleep(1)

asyncio.run(main())