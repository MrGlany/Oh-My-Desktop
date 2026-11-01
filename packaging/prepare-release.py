#!/usr/bin/python3
"""Build a release asset and AUR metadata; never commit, tag, publish or install."""
import argparse
import hashlib
import lzma
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ref', help='Existing Git release tag, e.g. v1.0.0')
parser.add_argument('--snapshot', action='store_true', help='Preview the working tree; not a published release')
parser.add_argument('--output', type=Path, required=True, help='New output directory outside the source tree')
args = parser.parse_args()
if bool(args.ref) == args.snapshot:
    parser.error('Choose exactly one: --ref vVERSION or --snapshot')
out = args.output.resolve()
if out == ROOT or ROOT in out.parents:
    parser.error('Output must be outside the source tree')
if out.exists():
    parser.error('Output already exists; use a new directory to preserve existing artifacts')
if args.snapshot:
    cmake = (ROOT / 'CMakeLists.txt').read_text()
else:
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', args.ref):
        parser.error('Release ref must be a vMAJOR.MINOR.PATCH tag')
    subprocess.run(['git', 'show-ref', '--verify', f'refs/tags/{args.ref}'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    cmake = subprocess.check_output(['git', 'show', f'refs/tags/{args.ref}:CMakeLists.txt'], cwd=ROOT, text=True)
match = re.search(r'project\(OhMyDesktop VERSION (\d+\.\d+\.\d+) LANGUAGES CXX\)', cmake)
if not match:
    parser.error('CMake must declare a three-part version')
version = match[1]
if args.ref and args.ref != f'v{version}':
    parser.error('Tag and CMake version differ')
name = f'oh-my-desktop-{version}'
out.mkdir(parents=True)
archive = out / f'{name}.tar.xz'
if args.snapshot:
    # Explicit roots exclude .git, build outputs and local configuration/secrets.
    with tarfile.open(archive, 'w:xz') as tar:
        def normalized(info):
            if '__pycache__' in Path(info.name).parts or info.name.endswith('.pyc'):
                return None
            if not (info.isfile() or info.isdir()):
                raise ValueError(f'Unexpected non-regular source: {info.name}')
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = 0
            return info
        for entry in ('CMakeLists.txt', 'LICENSE', 'README.md', '.gitignore', 'install.sh', 'assets', 'src', 'tests', 'packaging'):
            tar.add(ROOT / entry, arcname=f'{name}/{entry}', filter=normalized)
    template = (ROOT / 'packaging/aur/PKGBUILD.in').read_text()
else:
    # Archive the exact immutable tag, never an unrelated dirty working tree.
    with lzma.open(archive, 'wb') as compressed:
        process = subprocess.Popen(['git', 'archive', '--format=tar', f'--prefix={name}/', f'refs/tags/{args.ref}'], cwd=ROOT, stdout=subprocess.PIPE)
        while block := process.stdout.read(1024 * 1024):
            compressed.write(block)
        if process.wait():
            raise RuntimeError('git archive failed')
    template = subprocess.check_output(['git', 'show', f'refs/tags/{args.ref}:packaging/aur/PKGBUILD.in'], cwd=ROOT, text=True)
sha = hashlib.sha256(archive.read_bytes()).hexdigest()
aur = out / 'aur'
aur.mkdir()
(aur / 'PKGBUILD').write_text(template.replace('@VERSION@', version).replace('@SHA256@', sha))
# AUR requires .SRCINFO to match PKGBUILD exactly.
metadata = subprocess.check_output(['makepkg', '--printsrcinfo'], cwd=aur)
(aur / '.SRCINFO').write_bytes(metadata)
(out / 'SHA256SUMS').write_text(f'{sha}  {archive.name}\n')
if args.snapshot:
    (out / 'SNAPSHOT-NOT-FOR-PUBLISHING').write_text('Local test only. Regenerate with --ref vVERSION after committing and tagging.\n')
print(f'{"Preview" if args.snapshot else "Release"}: {archive}\nAUR: {aur}\nSHA256: {sha}')
