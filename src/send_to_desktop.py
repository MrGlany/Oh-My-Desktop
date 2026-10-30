#!/usr/bin/python3
"""Create no-overwrite symbolic desktop links. Never move or delete originals."""
import argparse
import json
import locale
import os
from pathlib import Path
import stat
import subprocess
import socket


def desktop_call(payload, address=None):
    address = address or os.environ.get('OMD_CONTROL_SOCKET') or str(Path(os.environ['XDG_RUNTIME_DIR'])/'oh-my-desktop-pages.sock')
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(1)
        client.connect(address)
        client.sendall(json.dumps(payload).encode()+b'\n')
        reply = b''
        while b'\n' not in reply:
            block = client.recv(4096)
            if not block or len(reply)+len(block)>65536:
                raise ValueError('Некорректный ответ рабочего стола')
            reply += block
        result = json.loads(reply.split(b'\n',1)[0])
    if not isinstance(result, dict) or not result.get('ok'):
        raise ValueError('Рабочий стол или выбранная страница недоступны')
    return result


def desktop_target(desktop):
    result = desktop_call({'command':'status','monitor':''})
    if not result.get('monitor') or not result.get('active'):
        raise ValueError('Не удалось определить активную страницу рабочего стола')
    if os.path.abspath(result.get('desktop','')) != os.path.abspath(desktop):
        raise ValueError('Приложение использует другую папку рабочего стола')
    return {'monitor':result['monitor'],'page':result['active']}


def place_created(created, target):
    errors = []
    for raw in created:
        try:
            desktop_call({'command':'place-links','monitor':target['monitor'],
                          'page':target['page'],'names':[Path(raw).name]})
        except (OSError, ValueError, KeyError) as exc:
            errors.append(f'{Path(raw).name}: ярлык создан, но не удалось назначить страницу ({exc}). Файл сохранён.')
    return errors


def link_name(source):
    source = Path(source)
    name = source.name
    if not source.is_dir() and source.suffix:
        stem, suffix = source.stem, source.suffix
        if suffix.lower() == '.desktop':
            try:
                names, main = {}, False
                for raw in source.read_text(encoding='utf-8').splitlines():
                    line = raw.strip()
                    if line.startswith('['):
                        main = line == '[Desktop Entry]'
                    elif main and not line.startswith('#') and '=' in line:
                        key, value = line.split('=', 1)
                        names[key.strip()] = value.strip()
                language = (locale.getlocale()[0] or '').split('_')[0]
                stem = names.get('Name[' + language + ']') or names.get('Name') or stem
            except (OSError, UnicodeError):
                pass
    else:
        stem, suffix = name, ''
    stem = ''.join(c if c.isprintable() and c not in '/\\' else ' ' for c in stem).strip(' .')
    stem = stem or 'Объект'
    while len(os.fsencode(stem + suffix)) > 180:
        if len(stem) > 1:
            stem = stem[:-1]
        else:
            suffix = suffix[:-1]
    return stem + ' — ярлык', suffix


def create_links(sources, desktop):
    desktop = Path(os.path.abspath(desktop))
    if not desktop.is_dir():
        raise ValueError('Папка рабочего стола не найдена: ' + str(desktop))
    results, errors = [], []
    for raw in sources:
        source = Path(os.path.abspath(raw))
        try:
            mode = source.stat().st_mode
            if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
                raise ValueError('Этот тип объекта не поддерживается')
            if source == desktop:
                raise ValueError('Нельзя создать ссылку рабочего стола на самого себя')
            base, suffix = link_name(source)
            for count in range(1, 10001):
                name = base + (f' ({count})' if count > 1 else '') + suffix
                destination = desktop / name
                try:
                    # symlink creation is atomic and refuses existing names,
                    # including dangling links. There is no shell or overwrite.
                    os.symlink(str(source), str(destination), target_is_directory=source.is_dir())
                    results.append(str(destination))
                    break
                except FileExistsError:
                    continue
            else:
                raise ValueError('Не удалось подобрать свободное имя')
        except (OSError, ValueError) as exc:
            errors.append(f'{source.name}: {exc}')
    return results, errors


def notify(title, message, error=False):
    try:
        subprocess.run(['/usr/bin/notify-send', '--app-name=Рабочий стол',
                        '--icon=' + ('dialog-error' if error else 'emblem-symbolic-link'),
                        '--', title, message], timeout=5, check=False)
    except (OSError, subprocess.TimeoutExpired):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quiet', action='store_true', help='Не показывать уведомления')
    parser.add_argument('sources', nargs='+')
    args = parser.parse_args()
    try:
        configured = subprocess.check_output(['xdg-user-dir', 'DESKTOP'], text=True).strip()
        if not configured or not Path(configured).is_absolute() or Path(configured) == Path.home():
            raise ValueError('В XDG user-dirs не задана отдельная папка рабочего стола')
        # Capture once, before creation: focus/page changes must not redirect links.
        target = desktop_target(configured)
        created, errors = create_links(args.sources, configured)
        errors += place_created(created, target)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        created, errors = [], [str(exc)]
    if created and not args.quiet:
        notify('Ярлыки созданы', f'На рабочем столе: {len(created)}. Оригиналы остались на месте.')
    if errors and not args.quiet:
        notify('Не все ярлыки созданы', '\n'.join(errors[:8]), True)
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
