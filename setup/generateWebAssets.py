#!/usr/bin/env python3
"""Regenerate all phone web UI assets embedded in firmware."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main() -> None:
    for script in ("generatePhoneAssets.py", "embedWebIndex.py"):
        subprocess.run([sys.executable, str(ROOT / script)], check=True)


if __name__ == "__main__":
    main()
