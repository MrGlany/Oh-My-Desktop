"""Test inline editing on an offscreen desktop with private files and settings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='properties-check-inline-rename-check-') as directory:
    root=Path(directory)
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',OMD_NO_ANIMATIONS='1',
             XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),XDG_CACHE_HOME=str(root/'cache'),
             XDG_STATE_HOME=str(root/'state'),OMD_DISABLE_DOLPHIN_MENU='1',
             OMD_DESKTOP_DIR=str(root/'inline-rename-check-desktop'))
    env.pop('HYPRLAND_INSTANCE_SIGNATURE',None)
    env.pop('DBUS_SESSION_BUS_ADDRESS',None)
    result=subprocess.run([str(Path(sys.argv[1]).resolve()),'--inline-rename-check'],env=env,timeout=40)
    sys.exit(result.returncode)
