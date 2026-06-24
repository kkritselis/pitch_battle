#!/usr/bin/env python3
"""Embed setup/esp_screen.gif for the round LCD (regenerate after editing the GIF)."""

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = Path(__file__).resolve().parent / "esp_screen.gif"
OUTPUT_PATH = PROJECT_ROOT / "include" / "esp_screen_gif.h"


def format_byte_array(data: bytes) -> str:
    lines = [
        f"constexpr size_t ESP_SCREEN_GIF_BYTES = {len(data)};",
        f"const uint8_t ESP_SCREEN_GIF[ESP_SCREEN_GIF_BYTES] PROGMEM = {{",
    ]
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
    if not SOURCE_PATH.is_file():
        raise SystemExit(f"Missing {SOURCE_PATH}")

    data = SOURCE_PATH.read_bytes()
    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// setup/esp_screen.gif embedded for full-screen round LCD playback.",
        "// Regenerate with: python3 setup/generateEspScreenGif.py",
        "",
        format_byte_array(data),
        "",
    ]
    OUTPUT_PATH.write_text("\n".join(sections))
    print(f"Wrote {OUTPUT_PATH} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
