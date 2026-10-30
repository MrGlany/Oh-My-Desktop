import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='properties-check-application-icons-') as directory:
    root=Path(directory)
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),XDG_STATE_HOME=str(root/'state'),XDG_CACHE_HOME=str(root/'cache'))
    env.pop('HYPRLAND_INSTANCE_SIGNATURE',None)
    result=subprocess.run([sys.argv[1],'--application-icons-check'],env=env,timeout=30)
    preview=root/'config/application-icons-fixture/real-app.png'
    if preview.exists():shutil.copy2(preview,'real-app-icon.png')
    print('application-icons-check exit',result.returncode,flush=True)
    sys.exit(result.returncode)
