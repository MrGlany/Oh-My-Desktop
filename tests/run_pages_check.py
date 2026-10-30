"""Run page migration/restart checks against generated files and virtual screens."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="properties-check-pages-check-") as directory:
        root = Path(directory)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", OMD_NO_ANIMATIONS="1",
                   XDG_CONFIG_HOME=str(root/"config"), XDG_DATA_HOME=str(root/"data"),
                   XDG_STATE_HOME=str(root/"state"), OMD_DESKTOP_DIR=str(root/"pages-check-desktop"))
        env.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
        for flag in ("--pages-check", "--pages-check-reload"):
            result = subprocess.run([binary, flag], env=env, timeout=60)
            print(flag, "exit", result.returncode, flush=True)
            if result.returncode:
                return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
