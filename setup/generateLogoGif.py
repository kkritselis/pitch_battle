#!/usr/bin/env python3
"""Embed setup/logo.gif as direct-push BLE windows (save_slot=0, not stored)."""

from pathlib import Path

from pypixelcolor.commands.send_image import send_image

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = Path(__file__).resolve().parent / "logo.gif"
OUTPUT_PATH = PROJECT_ROOT / "include" / "ipixel_logo_gif.h"


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


def main() -> None:
    if not SOURCE_PATH.is_file():
        raise SystemExit(f"Missing {SOURCE_PATH}")

    plan = send_image(str(SOURCE_PATH), save_slot=0)
    if not plan.windows:
        raise SystemExit("logo.gif produced no BLE windows")

    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// Direct-push logo GIF (save_slot=0). Not stored in a device slot.",
        "// Regenerate with: python3 setup/generateLogoGif.py",
        "",
        f"constexpr size_t IPIXEL_LOGO_GIF_WINDOW_COUNT = {len(plan.windows)};",
        "",
    ]

    for index, window in enumerate(plan.windows):
        sections.append(format_byte_array(f"IPIXEL_LOGO_GIF_WINDOW_{index}", window.data))
        sections.append("")

    sections.append("struct IpixelLogoGifWindow {")
    sections.append("  const uint8_t *data;")
    sections.append("  size_t length;")
    sections.append("};")
    sections.append("")
    sections.append("const IpixelLogoGifWindow IPIXEL_LOGO_GIF_WINDOWS[IPIXEL_LOGO_GIF_WINDOW_COUNT] = {")
    for index, window in enumerate(plan.windows):
        sections.append(
            f"  {{ IPIXEL_LOGO_GIF_WINDOW_{index}, IPIXEL_LOGO_GIF_WINDOW_{index}_BYTES }},"
        )
    sections.append("};")
    sections.append("")

    OUTPUT_PATH.write_text("\n".join(sections))
    print(
        f"Wrote {OUTPUT_PATH} ({len(plan.windows)} windows, "
        f"{sum(len(w.data) for w in plan.windows)} bytes total)"
    )


if __name__ == "__main__":
    main()
