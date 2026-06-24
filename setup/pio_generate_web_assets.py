Import("env")

import subprocess
import sys
from pathlib import Path

root = Path(env["PROJECT_DIR"])
script = root / "setup" / "generateWebAssets.py"
subprocess.run([sys.executable, str(script)], check=True)
