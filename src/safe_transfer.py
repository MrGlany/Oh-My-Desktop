#!/usr/bin/python3
"""Conservative Linux file transactions. Never unlink user data.

Replaced targets and cross-device move sources are retained in private recovery
folders. Interrupted transactions may require manual recovery; no auto-cleanup.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile


class TransferError(Exception):
    pass


def exists(path):
    return os.path.lexists(path)


def rename_new(src, dst):
    # Atomic no-overwrite commit, including dangling symlinks and racing writers.
    libc = ctypes.CDLL(None, use_errno=True)
    fn = libc.renameat2
    fn.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    if fn(-100, os.fsencode(src), -100, os.fsencode(dst), 1):
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(dst))


def snapshot(path):
    """Content and metadata manifest; do not follow symlinks or special files."""
    result = {}
    def visit(p, rel):
        before = os.lstat(p)
        mode = before.st_mode
        if stat.S_ISLNK(mode):
            value = ('link', os.readlink(p))
        elif stat.S_ISREG(mode):
            digest = hashlib.sha256()
            fd = os.open(p, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(fd, 'rb') as f:
                opened = os.fstat(f.fileno())
                if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
                    raise TransferError('Source changed while opening')
                for block in iter(lambda: f.read(1024 * 1024), b''):
                    digest.update(block)
            value = ('file', digest.hexdigest(), before.st_size)
        elif stat.S_ISDIR(mode):
            value = ('dir',)
            with os.scandir(p) as entries:
                names = sorted(e.name for e in entries)
            for name in names:
                visit(p / name, rel + '/' + name)
        else:
            raise TransferError('Special files are not supported: ' + str(p))
        after = os.lstat(p)
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise TransferError('Source changed during verification: ' + str(p))
        # atime is intentionally excluded (reads can change it).
        result[rel] = (value, stat.S_IMODE(mode), before.st_mtime_ns,
                       tuple(sorted((k, os.getxattr(p, k, follow_symlinks=False))
                                    for k in os.listxattr(p, follow_symlinks=False))))
    visit(Path(path), '')
    return result


def checked_paths(src, dst):
    src, dst = Path(os.path.abspath(src)), Path(os.path.abspath(dst))
    if not exists(src):
        raise TransferError('Source does not exist')
    if not src.name or not dst.name:
        raise TransferError('Filesystem roots are forbidden')
    # Disallow symlinked parent paths; don't follow a changing alias accidentally.
    for p in (src.parent, dst.parent):
        if p.resolve(strict=True) != p:
            raise TransferError('Symlinked parent paths are not supported')
        if not p.is_dir():
            raise TransferError('Parent is not a directory')
    if src == dst:
        raise TransferError('Source and destination are the same')
    if exists(dst) and (os.lstat(src).st_dev, os.lstat(src).st_ino) == (os.lstat(dst).st_dev, os.lstat(dst).st_ino):
        raise TransferError('Source and destination alias the same inode')
    if not src.is_symlink() and src.is_dir() and src in dst.parents:
        raise TransferError('Destination is inside source')
    if exists(dst) and not dst.is_symlink() and dst.is_dir() and dst in src.parents:
        raise TransferError('Destination contains source')
    return src, dst


def transfer(src, dst, move=False, replace=False):
    src, dst = checked_paths(src, dst)
    target_exists = exists(dst)
    if target_exists and not replace:
        raise TransferError('Destination already exists')
    target_stat = os.lstat(dst) if target_exists else None
    # Validate all entries even for a same-device move: fail closed on special files.
    original = snapshot(src)
    recovery = []
    stage = None
    prepared = src
    cross_device = os.lstat(src).st_dev != os.stat(dst.parent).st_dev
    if not move or cross_device:
        stage = Path(tempfile.mkdtemp(prefix='.omd-stage-', dir=dst.parent))
        prepared = stage / 'payload'
        try:
            if src.is_symlink():
                shutil.copy2(src, prepared, follow_symlinks=False)
            elif src.is_dir():
                shutil.copytree(src, prepared, symlinks=True, copy_function=shutil.copy2)
            else:
                shutil.copy2(src, prepared, follow_symlinks=False)
            if snapshot(prepared) != original or snapshot(src) != original:
                raise TransferError('Copy verification failed; originals retained')
        except Exception as exc:
            # Retain stage for diagnosis. Never recursively delete a path here.
            raise TransferError(f'{exc}; staging retained at {stage}') from exc
    backup = None
    if target_exists:
        now = os.lstat(dst)
        if (now.st_dev, now.st_ino, now.st_mtime_ns, now.st_ctime_ns) != (target_stat.st_dev, target_stat.st_ino, target_stat.st_mtime_ns, target_stat.st_ctime_ns):
            raise TransferError('Destination changed; refusing replacement')
        backup_dir = Path(tempfile.mkdtemp(prefix='.omd-recovery-', dir=dst.parent))
        backup = backup_dir / dst.name
        rename_new(dst, backup)
        recovery.append(str(backup))
    try:
        rename_new(prepared, dst)
    except Exception:
        if backup is not None:
            # If another writer claimed dst, preserve BOTH rather than overwrite it.
            try:
                rename_new(backup, dst)
            except OSError:
                pass
        raise
    if move and cross_device:
        # Deliberately retain the source; no recursive deletion can race a writer.
        # Rename only if its current verified state is still the copied state.
        try:
            if snapshot(src) != original:
                raise TransferError('Source changed after copy')
            source_dir = Path(tempfile.mkdtemp(prefix='.omd-recovery-', dir=src.parent))
            source_backup = source_dir / src.name
            rename_new(src, source_backup)
            recovery.append(str(source_backup))
        except Exception as exc:
            recovery.append(f'Source retained at {src}: {exc}')
    if stage is not None:
        try:
            stage.rmdir()  # empty, private staging directory only
        except OSError:
            pass
    if recovery:
        try:
            state = Path(os.environ.get('XDG_STATE_HOME', str(Path.home()/'.local/state'))) / 'oh-my-desktop'
            state.mkdir(parents=True, exist_ok=True, mode=0o700)
            fd = os.open(state/'recovery.jsonl', os.O_APPEND | os.O_CREAT | os.O_WRONLY, 0o600)
            with os.fdopen(fd, 'a') as log:
                log.write(json.dumps({'source':str(src), 'destination':str(dst), 'recovery':recovery}) + '\n')
        except OSError as exc:
            recovery.append('Recovery registry unavailable: ' + str(exc))
    return {'ok': True, 'recovery': recovery}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--move', action='store_true')
    parser.add_argument('--replace', action='store_true')
    parser.add_argument('source')
    parser.add_argument('destination')
    args = parser.parse_args()
    try:
        print(json.dumps(transfer(args.source, args.destination, args.move, args.replace)))
    except Exception as exc:
        print(json.dumps({'ok': False, 'error': str(exc)}))
        raise SystemExit(1)
