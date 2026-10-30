import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='properties-check-missing-app-') as directory:
    root=Path(directory)
    (root/'data/Trash/files').mkdir(parents=True)
    (root/'data/Trash/info').mkdir()
    (root/'config').mkdir()
    (root/'config/kdeglobals').write_text('[Colors:Window]\nBackgroundNormal=32,32,39\nForegroundNormal=230,230,240\n')
    env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),XDG_STATE_HOME=str(root/'state'))
    result=subprocess.run([sys.argv[1],'--missing-app-check'],env=env,timeout=30)
    print("missing-app-check exit",result.returncode,flush=True)
    sys.exit(result.returncode)
