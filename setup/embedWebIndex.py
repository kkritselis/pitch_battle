#!/usr/bin/env python3
"""Embed src/index.html for the ESP32 web server (regenerate after editing HTML)."""

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = PROJECT_ROOT / "src" / "index.html"
OUTPUT_PATH = PROJECT_ROOT / "include" / "web_index.h"
DELIMITER = "WEBINDEX"


def main() -> None:
    if not SOURCE_PATH.is_file():
        raise SystemExit(f"Missing {SOURCE_PATH}")

    html = SOURCE_PATH.read_text()
    if f"){DELIMITER}" in html:
        raise SystemExit(f"index.html must not contain the raw string delimiter ){DELIMITER}\"")

    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "// src/index.html embedded for the phone web UI.",
        "// Regenerate with: python3 setup/embedWebIndex.py",
        "",
        f"constexpr size_t WEB_INDEX_HTML_BYTES = {len(html.encode('utf-8'))};",
        f"const char WEB_INDEX_HTML[] PROGMEM = R\"{DELIMITER}(",
        html.rstrip("\n"),
        f"){DELIMITER}\";",
        "",
    ]
    OUTPUT_PATH.write_text("\n".join(sections))
    print(f"Wrote {OUTPUT_PATH} ({len(html.encode('utf-8'))} bytes)")


if __name__ == "__main__":
    main()
