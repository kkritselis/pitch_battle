#!/usr/bin/env python3
"""Embed src/phone_*.jpg for the phone web UI (regenerate after editing JPGs)."""

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = PROJECT_ROOT / "src"
OUTPUT_PATH = PROJECT_ROOT / "include" / "phone_assets.h"

ASSETS = [
    ("phone_header.jpg", "PHONE_HEADER_JPG"),
    ("phone_count.jpg", "PHONE_COUNT_JPG"),
    ("phone_field.jpg", "PHONE_FIELD_JPG"),
    ("phone_pvp.jpg", "PHONE_PVP_JPG"),
    ("phone_choice.jpg", "PHONE_CHOICE_JPG"),
    ("phone_button.jpg", "PHONE_BUTTON_JPG"),
]


def format_byte_array(symbol: str, data: bytes) -> list[str]:
    lines = [
        f"constexpr size_t {symbol}_BYTES = {len(data)};",
        f"const uint8_t {symbol}[{symbol}_BYTES] PROGMEM = {{",
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
    return lines


def main() -> None:
    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// src/phone_*.jpg embedded for the phone web UI.",
        "// Regenerate with: python3 setup/generatePhoneAssets.py",
        "",
    ]

    for filename, symbol in ASSETS:
        path = SOURCE_DIR / filename
        if not path.is_file():
            raise SystemExit(f"Missing {path}")
        data = path.read_bytes()
        sections.extend(format_byte_array(symbol, data))
        sections.append("")

    OUTPUT_PATH.write_text("\n".join(sections))
    total = sum((SOURCE_DIR / filename).stat().st_size for filename, _ in ASSETS)
    print(f"Wrote {OUTPUT_PATH} ({total} bytes across {len(ASSETS)} JPGs)")


if __name__ == "__main__":
    main()
