#!/usr/bin/python3
"""Read-only app icon discovery. Application binaries are never executed."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile

MAX_IMAGE=16*1024*1024
EXTENSIONS=('.svg','.png','.ico','.xpm','.webp','.jpg','.jpeg')

def image_file(path):
    try:
        p=Path(path)
        return p.is_file() and p.stat().st_size<=MAX_IMAGE and (p.suffix.lower() in EXTENSIONS or p.name=='.DirIcon')
    except OSError:return False

def entry(path):
    fields={};main=False
    try:
        with open(path,encoding='utf-8') as f:text=f.read(256*1024)
        for line in text.splitlines():
            line=line.strip()
            if line.startswith('['):main=line=='[Desktop Entry]'
            elif main and not line.startswith('#') and '=' in line:
                k,v=line.split('=',1);fields[k.strip()]=v.strip()
    except (OSError,UnicodeError):pass
    return fields

def normalize(text):return ''.join(c.lower() for c in text if c.isalnum())

def executable(command):
    try:args=shlex.split(command)
    except ValueError:return ''
    if args and Path(args[0]).name=='env':
        args.pop(0)
        while args and re.match(r'^[A-Za-z_]\w*=',args[0]):args.pop(0)
    return args[0] if args and not args[0].startswith('-') else ''

def icon_value(value,root):
    if not value:return None
    p=Path(value)
    if p.is_absolute():return {'path':str(p)} if image_file(p) else None
    if '/' in value:
        p=root/p
        return {'path':str(p.resolve())} if image_file(p) else None
    if not re.fullmatch(r'[\w.+-]+',value):return None
    for directory in (root/'share/icons/hicolor',root/'icons/hicolor'):
        if directory.is_dir():
            candidates=[]
            for size in sorted(directory.iterdir())[:64]:
                for ext in EXTENSIONS:
                    icon=size/'apps'/(value+ext)
                    if image_file(icon):
                        match=re.match(r'(\d+)x',size.name)
                        rank=100000 if size.name=='scalable' else int(match.group(1)) if match else 0
                        candidates.append((rank,str(icon)))
            if candidates:return {'path':max(candidates)[1]}
    for directory in (root/'share/pixmaps',root/'pixmaps',root):
        for ext in EXTENSIONS:
            p=directory/(value if Path(value).suffix.lower() in EXTENSIONS else value+ext)
            if image_file(p):return {'path':str(p)}
    return {'theme':value}

def desktop_match(path,binary,local):
    fields=entry(path);value=fields.get('Icon','')
    if not value or fields.get('Type','Application')!='Application':return 0,fields
    command=executable(fields.get('Exec',''))
    if command:
        if os.path.isabs(command) and Path(command).resolve()==binary:return 100,fields
        if Path(command).name.lower()==binary.name.lower():return 90,fields
    if local and normalize(fields.get('Name',''))==normalize(binary.stem):return 80,fields
    return 0,fields

def run_tool(args,timeout=3):
    # Bound stdout on disk, without extracting archive members to the filesystem.
    try:
        with tempfile.TemporaryFile() as output:
            result=subprocess.run(args,stdout=output,stderr=subprocess.DEVNULL,timeout=timeout,check=False)
            if result.returncode:return b''
            if output.tell()>MAX_IMAGE:return b''
            output.seek(0);return output.read(MAX_IMAGE+1)
    except (OSError,subprocess.SubprocessError):return b''

def save_embedded(binary,cache,data,suffix):
    if not data or len(data)>MAX_IMAGE:return None
    info=binary.stat();key=hashlib.sha256((str(binary)+'|'+str(info.st_mtime_ns)+'|'+str(info.st_size)).encode()).hexdigest()
    directory=Path(cache);directory.mkdir(parents=True,exist_ok=True,mode=0o700)
    destination=directory/(key+suffix)
    fd,temp=tempfile.mkstemp(prefix='.icon-',dir=directory)
    try:
        with os.fdopen(fd,'wb') as f:f.write(data)
        os.replace(temp,destination)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return {'path':str(destination),'source':'embedded'}

def embedded(binary,cache):
    if binary.suffix.lower()=='.exe':
        tool=shutil.which('wrestool')
        if tool:return save_embedded(binary,cache,run_tool([tool,'-x','--type=14','--',str(binary)]),'.ico')
        return None
    try:
        with binary.open('rb') as f:
            head=f.read(12)
            if head[:4]!=b'\x7fELF' or head[8:11]!=b'AI\x02':return None
            tool=shutil.which('unsquashfs')
            if not tool:return None
            f.seek(0);offset=None;previous=b'';position=0
            while position<64*1024*1024:
                block=f.read(1024*1024)
                if not block:break
                data=previous+block;index=data.find(b'hsqs')
                if index>=0:offset=position-len(previous)+index;break
                previous=data[-3:];position+=len(block)
        if offset is None:return None
        prefix=[tool,'-o',str(offset)]
        data=run_tool(prefix+['-cat',str(binary),'.DirIcon'])
        suffix='.png'
        if not data:
            listing=run_tool(prefix+['-ll',str(binary),'.DirIcon']).decode('utf-8',errors='replace')
            match=re.search(r'squashfs-root/\.DirIcon -> (.+)',listing)
            if not match:return None
            member=posixpath.normpath(match.group(1).strip())
            if member.startswith('/') or member=='..' or member.startswith('../'):return None
            suffix=PurePosixPath(member).suffix.lower()
            if suffix not in EXTENSIONS:return None
            data=run_tool(prefix+['-cat',str(binary),member])
        if data.lstrip().startswith((b'<svg',b'<?xml')):suffix='.svg'
        return save_embedded(binary,cache,data,suffix)
    except OSError:return None

def discover(path,cache):
    try:
        binary=Path(path).resolve(strict=True)
        if not binary.is_file():return {}
        if binary.suffix.lower()=='.desktop':
            fields=entry(binary)
            found=icon_value(fields.get('Icon',''),binary.parent)
            return dict(found,source='desktop-entry') if found else {}
        if not (binary.stat().st_mode&0o111 or binary.suffix.lower() in ('.exe','.appimage')):return {}
        roots=[binary.parent]
        if binary.parent.name in ('bin','sbin'):roots.append(binary.parent.parent)
        # Prefer exact bundle/installed metadata to arbitrary nearby images.
        matches=[]
        for root in roots:
            for directory in (root/'share/applications',root/'applications'):
                if not directory.is_dir():continue
                for desktop in sorted(directory.glob('*.desktop'))[:128]:
                    score,fields=desktop_match(desktop,binary,True)
                    if score:
                        found=icon_value(fields['Icon'],root)
                        if found:matches.append((score,found))
        for directory in roots:
            for desktop in sorted(directory.glob('*.desktop'))[:64]:
                score,fields=desktop_match(desktop,binary,True)
                if score:
                    found=icon_value(fields['Icon'],directory)
                    if found:matches.append((score,found))
        if matches:return dict(max(matches,key=lambda x:x[0])[1],source='bundle')
        data_roots=[Path(os.environ.get('XDG_DATA_HOME',str(Path.home()/'.local/share')))]
        data_roots += [Path(p) for p in os.environ.get('XDG_DATA_DIRS','/usr/local/share:/usr/share').split(':') if p]
        data_roots += [Path.home()/'.local/share/flatpak/exports/share',Path('/var/lib/flatpak/exports/share')]
        for root in dict.fromkeys(data_roots):
            for desktop in sorted((root/'applications').glob('*.desktop'))[:4096]:
                score,fields=desktop_match(desktop,binary,False)
                if score:
                    found=icon_value(fields['Icon'],root)
                    if found:matches.append((score,found))
        if matches:return dict(max(matches,key=lambda x:x[0])[1],source='installed-metadata')
        found=embedded(binary,cache)
        if found:return found
        for root in roots:
            for name in (binary.name,binary.stem,'icon','logo'):
                for ext in EXTENSIONS:
                    p=root/(name+ext)
                    if image_file(p):return {'path':str(p),'source':'sidecar'}
            p=root/'.DirIcon'
            if image_file(p):return {'path':str(p.resolve()),'source':'sidecar'}
            for relative in ('data/flutter_assets/assets/icon.png','data/flutter_assets/assets/images/logo.png','data/flutter_assets/assets/icons/app_icon.png'):
                p=root/relative
                if image_file(p):return {'path':str(p),'source':'flutter-assets'}
    except (OSError,ValueError):pass
    return {}

if __name__=='__main__':
    import resource
    resource.setrlimit(resource.RLIMIT_FSIZE,(MAX_IMAGE,MAX_IMAGE))
    resource.setrlimit(resource.RLIMIT_CPU,(8,8))
    print(json.dumps(discover(sys.argv[1],sys.argv[2]),ensure_ascii=False))
