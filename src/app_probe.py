#!/usr/bin/python3
"""Read-only availability check for desktop launchers. Never execute their Exec."""
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


def executable_status(command):
    if not command or '%' in command:
        return 'unknown'
    if command.startswith(('/run/media/', '/media/', '/mnt/')):
        return 'available' if os.path.isfile(command) and os.access(command, os.X_OK) else 'unavailable'
    if '/' in command:
        if not os.path.isabs(command):
            return 'unknown'
        if not os.path.exists(command):
            return 'missing'
        return 'available' if os.path.isfile(command) and os.access(command, os.X_OK) else 'unavailable'
    if shutil.which(command):return 'available'
    for directory in os.get_exec_path():
        if os.path.lexists(os.path.join(directory,command)):
            return 'unavailable'
    return 'missing'


def flatpak_status(arguments):
    # Query only the system Flatpak tool, never an arbitrary launcher binary.
    if not os.path.isfile('/usr/bin/flatpak'):
        return 'unknown'
    args=list(arguments);scopes=('--user','--system');branch=None;arch=None
    while args and args[0] in ('--user', '--system'):
        scopes=(args.pop(0),)
    if not args or args.pop(0) != 'run':
        return 'unknown'
    app=None
    flags={'--file-forwarding','--devel','--sandbox','--no-a11y-bus','--no-documents-portal','--log-session-bus','--log-system-bus'}
    values={'--arch','-a','--branch','-b','--command','--cwd','--runtime','--runtime-version'}
    while args:
        arg=args.pop(0)
        if arg in values:
            if not args:return 'unknown'
            value=args.pop(0)
            if arg in ('--branch','-b'):branch=value
            if arg in ('--arch','-a'):arch=value
        elif arg.startswith('--branch='):
            branch=arg.split('=',1)[1]
        elif arg.startswith('--arch='):
            arch=arg.split('=',1)[1]
        elif arg in flags or (arg.startswith('--') and '=' in arg):
            continue
        elif arg.startswith('-'):
            return 'unknown'
        else:
            app=arg;break
    if not app or not re.fullmatch(r'[A-Za-z_][\w-]*(?:\.[A-Za-z_][\w-]*){2,}',app):
        return 'unknown'
    if any(value is not None and not re.fullmatch(r'[A-Za-z0-9._-]+',value) for value in (branch,arch)):
        return 'unknown'
    missing=[]
    for scope in scopes:
        try:
            command=['/usr/bin/flatpak',scope,'info','--show-ref']
            if arch:command.append('--arch='+arch)
            command.append(app)
            if branch:command.append(branch)
            result=subprocess.run(command,capture_output=True,text=True,timeout=2,env=dict(os.environ,LC_ALL='C',LANG='C'))
        except (OSError,subprocess.SubprocessError):
            missing.append(False);continue
        if result.returncode==0:return 'available'
        missing.append('not installed' in result.stderr.lower())
    return 'missing' if all(missing) else 'unknown'


def probe(path):
    entries={};main=False
    try:
        with open(path,'r',encoding='utf-8') as source:
            content=source.read(1024*1024+1)
        if len(content)>1024*1024:return {'status':'unknown'}
        for line in content.splitlines():
            line=line.strip()
            if line.startswith('['):main=line=='[Desktop Entry]'
            elif main and not line.startswith('#') and '=' in line:
                key,value=line.split('=',1);entries[key.strip()]=value.strip()
    except (OSError,UnicodeError):return {'status':'unknown'}
    result={'status':'unknown','name':entries.get('Name','')}
    if entries.get('Type','Application')!='Application':return result
    command=entries.get('Exec','')
    # Shell scripts, substitutions and compound commands cannot be classified
    # reliably without running them. Preserve their existing launch behaviour.
    if not command or re.search(r'[;&|<>`$\n\r]',command):return result
    try:parts=shlex.split(command)
    except ValueError:return result
    if not parts:return result
    if parts[0] in ('env','/usr/bin/env','/bin/env'):
        parts.pop(0)
        while parts and re.match(r'^[A-Za-z_]\w*=',parts[0]):
            if parts[0].startswith('PATH='):return result
            parts.pop(0)
        if not parts or parts[0].startswith('-'):return result
    if parts[0] in ('sh','bash','fish','zsh','/bin/sh','/bin/bash','/usr/bin/bash','/usr/bin/fish'):
        return result
    status=executable_status(parts[0])
    if status=='available' and parts[0] in ('flatpak','/usr/bin/flatpak'):
        status=flatpak_status(parts[1:])
    if status=='available' and entries.get('TryExec'):
        declared=entries['TryExec']
        if any(char in declared for char in ('"',"'",'\\')) or (not os.path.isabs(declared) and any(char.isspace() for char in declared)):
            status='unknown'
        else:status=executable_status(declared)
    result.update(status=status,command=parts[0])
    return result


if __name__=='__main__':
    print(json.dumps(probe(sys.argv[1]),ensure_ascii=False))
