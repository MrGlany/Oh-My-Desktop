#!/usr/bin/python3
"""Resolve an existing desktop symlink; mount its disk only on explicit activation.

No shell, sudo, fstab changes, link rewriting, or automatic mount at startup.
UDisks performs its normal authorization through the session's polkit agent.
"""
import json
import os
import pwd
import subprocess
import sys
from pathlib import Path


class ShortcutError(Exception):
    pass


def block_devices():
    result = subprocess.run(
        ["/usr/bin/lsblk", "--json", "--paths", "--output",
         "NAME,LABEL,UUID,FSTYPE,MOUNTPOINTS"],
        check=True, capture_output=True, text=True, timeout=15)
    def walk(items):
        for item in items:
            yield item
            yield from walk(item.get("children", []))
    return list(walk(json.loads(result.stdout)["blockdevices"]))


def media_target(target):
    root = Path("/run/media") / pwd.getpwuid(os.getuid()).pw_name
    try:
        relative = target.relative_to(root)
    except ValueError:
        raise ShortcutError("Цель ярлыка недоступна. Подключите диск в Dolphin и повторите открытие.")
    if not relative.parts or relative.parts[0] in (".", ".."):
        raise ShortcutError("Не удалось определить диск этого ярлыка.")
    return root, relative.parts[0], relative.parts[1:]


def accessible(path):
    try:
        path.stat()
        return True
    except FileNotFoundError:
        return False
    except PermissionError:
        raise ShortcutError("Нет доступа к цели ярлыка. Проверьте права в Dolphin.")


def resolve_shortcut(link):
    link = Path(os.path.abspath(link))
    if not link.is_symlink():
        raise ShortcutError("Ярлык больше не является символической ссылкой.")
    raw = os.readlink(link)
    target = Path(os.path.abspath(link.parent / raw))
    if accessible(target):
        return str(target)
    root, label, tail = media_target(target)
    devices = [d for d in block_devices()
               if d.get("fstype") and label in (d.get("label"), d.get("uuid"))]
    # A label alone cannot safely distinguish two disks. Never guess.
    if len(devices) != 1:
        raise ShortcutError("Диск не найден или его имя неоднозначно. Подключите нужный диск в Dolphin.")
    device = devices[0]
    if any(device.get("mountpoints") or []):
        raise ShortcutError("Диск уже подключён, но цель ярлыка отсутствует. Проверьте её путь в Dolphin.")
    name = device.get("name", "")
    if not name.startswith("/dev/") or "\x00" in name:
        raise ShortcutError("Некорректное имя устройства.")
    result = subprocess.run(
        ["/usr/bin/udisksctl", "mount", "--block-device", name],
        capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise ShortcutError("Диск не подключён: авторизация отменена или подключение не удалось.\n"
                            + result.stderr.strip()[:1200])
    # Re-read authoritative metadata; never parse localized udisksctl output.
    mounted = [d for d in block_devices()
               if d.get("name") == name and d.get("uuid") == device.get("uuid")]
    expected = str(root / label)
    if len(mounted) != 1 or expected not in (mounted[0].get("mountpoints") or []):
        raise ShortcutError("Диск подключён по другому пути. Откройте его в Dolphin и обновите ярлык.")
    if not link.is_symlink() or os.readlink(link) != raw:
        raise ShortcutError("Ярлык изменился во время подключения. Откройте его ещё раз.")
    if not accessible(target):
        raise ShortcutError("Диск подключён, но цель ярлыка не найдена. Проверьте её путь в Dolphin.")
    return str(target)


def main():
    try:
        if len(sys.argv) != 2:
            raise ShortcutError("Не указан ярлык.")
        path = resolve_shortcut(sys.argv[1])
        print(json.dumps({"path": path}, ensure_ascii=False))
        return 0
    except (ShortcutError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(json.dumps({"error": str(error)}, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    sys.exit(main())
