#!/usr/bin/python3
"""Inspect an extracted package without installing or starting its desktop UI."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--source', type=Path, required=True)
a = parser.parse_args()
root = a.root.resolve()
source = a.source.resolve()
private = root / 'usr/lib/oh-my-desktop'
expected = {'oh-my-desktop', 'safe_transfer.py', 'open_shortcut.py', 'app_probe.py', 'application_icon.py', 'send-to-desktop.py'}
assert {p.name for p in private.iterdir()} == expected
assert not (root / 'home').exists()
assert not (root / 'etc').exists()
assert not (root / '.INSTALL').exists()
assert not list(root.rglob('*.pyc'))
for name in expected - {'oh-my-desktop'}:
    original = 'send_to_desktop.py' if name == 'send-to-desktop.py' else name
    assert (private / name).read_bytes() == (source / 'src' / original).read_bytes(), name
wrapper = root / 'usr/bin/oh-my-desktop'
assert os.access(wrapper, os.X_OK)
assert 'exec "/usr/lib/oh-my-desktop/oh-my-desktop" "$@"' in wrapper.read_text()
assert (root / 'usr/bin/oh-my-desktop-pages').read_bytes() == (source / 'src/desktop_pages.py').read_bytes()
version = subprocess.check_output([private / 'oh-my-desktop', '--version'], text=True).strip()
expected_version = re.search(r'project\(OhMyDesktop VERSION ([0-9.]+)', (source / 'CMakeLists.txt').read_text())[1]
assert version == f'Oh My Desktop {expected_version}', version
service = (root / 'usr/lib/systemd/user/oh-my-desktop.service').read_text()
assert 'KillMode=process\n' in service
assert 'ExecStart=/usr/bin/oh-my-desktop\n' in service
assert 'ExecStopPost=/usr/lib/oh-my-desktop/oh-my-desktop --cleanup-dolphin-menu\n' in service
assert not list(root.rglob('*.wants'))
subprocess.run(['desktop-file-validate', root / 'usr/share/applications/io.github.MrGlany.OhMyDesktop.desktop'], check=True)
# Use the installed helper, on independent fixtures only. Never launch a GUI.
with tempfile.TemporaryDirectory(prefix='omd-package-check-') as tmp:
    base = Path(tmp)
    src = base / 'source'
    dst = base / 'copy'
    src.mkdir()
    (src / '.hidden').write_bytes(b'hidden\x00content')
    (src / 'link').symlink_to('missing-target')
    subprocess.run(['/usr/bin/python3', '-I', private / 'safe_transfer.py', str(src), str(dst)], check=True, capture_output=True)
    assert (dst / '.hidden').read_bytes() == (src / '.hidden').read_bytes()
    assert (dst / 'link').is_symlink()
    assert os.readlink(dst / 'link') == 'missing-target'
    assert src.is_dir()
print(f'Package layout, version, helpers, safe copy, service and desktop entry: OK ({version})')
