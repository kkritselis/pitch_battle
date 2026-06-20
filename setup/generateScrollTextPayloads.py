#!/usr/bin/env python3
"""Generate pre-encoded iPixel scroll-text BLE messages for the firmware."""

from pathlib import Path

from pypixelcolor.commands.send_text import send_text

PROJECT_ROOT = Path(__file__).resolve().parents[1]
OUTPUT_PATH = PROJECT_ROOT / "include" / "ipixel_scroll_text.h"

# animation 1 = horizontal scroll on 96x16 panels (pypixelcolor send_text)
SCROLL_TEXTS = {
    "IPIXEL_SCROLL_WALK": ("WALK", 1),
    "IPIXEL_SCROLL_STRIKEOUT": ("STRIKEOUT", 1),
}


def format_byte_array(name: str, data: bytes) -> str:
    lines = [f"constexpr size_t {name}_BYTES = {len(data)};", f"const uint8_t {name}[{name}_BYTES] = {{"]
    row = "  "
    for i, byte in enumerate(data):
        row += f"0x{byte:02X}, "
        if (i + 1) % 12 == 0:
            lines.append(row.rstrip())
            row = "  "
    if row.strip():
        lines.append(row.rstrip())
    lines.append("};")
    return "\n".join(lines)


def main():
    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// Pre-encoded send_text frames (animation=1 scroll, char_height=16, save_slot=0).",
        "// Regenerate with: python3 setup/generateScrollTextPayloads.py",
        "",
    ]

    for symbol, (text, animation) in SCROLL_TEXTS.items():
        plan = send_text(
            text,
            animation=animation,
            speed=80,
            color="ffffff",
            char_height=16,
            save_slot=0,
        )
        if len(plan.windows) != 1:
            raise SystemExit(f"{text} split into {len(plan.windows)} windows; firmware expects one")

        data = plan.windows[0].data
        sections.append(format_byte_array(symbol, data))
        sections.append("")

    OUTPUT_PATH.write_text("\n".join(sections))
    print(f"Wrote {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
