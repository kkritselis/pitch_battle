import argparse
import asyncio
from pathlib import Path

from PIL import Image
from pypixelcolor import AsyncClient


ADDRESS = "80CE82D9-A461-A4D3-85E2-40A6D737DDEA"
PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = PROJECT_ROOT / "assets" / "ipixel"
TEMPLATE_PATH = ASSET_DIR / "scoreboard_template.png"
OUTPUT_PATH = ASSET_DIR / "scoreboard_test.png"

CYAN = (38, 188, 200)
YELLOW = (225, 246, 47)
RED = (255, 0, 0)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

DIGITS = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
}


def draw_digit(img: Image.Image, x: int, y: int, value: int, color: tuple[int, int, int]) -> None:
    digit = str(max(0, min(9, int(value))))
    pixels = img.load()
    for row, bits in enumerate(DIGITS[digit]):
        for col, bit in enumerate(bits):
            if bit == "1":
                pixels[x + col, y + row] = color


def fill_rect(img: Image.Image, x: int, y: int, w: int, h: int, color: tuple[int, int, int]) -> None:
    pixels = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            pixels[xx, yy] = color


def draw_total(img: Image.Image, x: int, y: int, value: int, color: tuple[int, int, int]) -> None:
    value = max(0, min(99, int(value)))
    if value < 10:
        draw_digit(img, x + 2, y, value, color)
        return

    draw_digit(img, x, y, value // 10, color)
    draw_digit(img, x + 4, y, value % 10, color)


def draw_score_row(
    img: Image.Image,
    y: int,
    inning_runs: list[int],
    total: int,
    color: tuple[int, int, int],
) -> None:
    for x, value in zip((66, 72, 78), inning_runs):
        fill_rect(img, x, y, 5, 6, BLACK)
        draw_digit(img, x + 1, y, value, color)

    fill_rect(img, 86, y, 8, 6, BLACK)
    draw_total(img, 87, y, total, color)


def render_test_scoreboard() -> Image.Image:
    img = Image.open(TEMPLATE_PATH).convert("RGB")

    draw_digit(img, 0, 0, 0, CYAN)     # balls
    draw_digit(img, 0, 5, 1, YELLOW)   # strikes
    draw_digit(img, 0, 10, 0, CYAN)    # outs

    fill_rect(img, 37, 1, 2, 2, WHITE) # second
    fill_rect(img, 43, 7, 2, 2, RED)   # first
    fill_rect(img, 31, 7, 2, 2, WHITE) # third

    draw_score_row(img, 1, [0, 0, 0], 0, CYAN)
    draw_score_row(img, 9, [1, 0, 0], 1, YELLOW)
    return img


async def send_image(save_slot: int) -> None:
    async with AsyncClient(ADDRESS) as device:
        print(f"Uploading {OUTPUT_PATH} with save_slot={save_slot}")
        await device.send_image(str(OUTPUT_PATH), save_slot=save_slot)
        if save_slot:
            print(f"Showing slot {save_slot}")
            await device.show_slot(save_slot)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--send", action="store_true", help="send image to iPixel with pypixelcolor")
    parser.add_argument("--save-slot", type=int, default=9)
    args = parser.parse_args()

    image = render_test_scoreboard()
    image.save(OUTPUT_PATH, format="PNG", optimize=True)
    print(f"Wrote {OUTPUT_PATH} ({OUTPUT_PATH.stat().st_size} bytes)")

    if args.send:
        asyncio.run(send_image(args.save_slot))


if __name__ == "__main__":
    main()
