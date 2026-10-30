#!/usr/bin/python3
"""Switch icon pages on Hyprland's focused monitor without switching workspaces."""
import json
import os
import socket
import sys
from pathlib import Path


def request(command, monitor, page=None, address=None):
    data = {"monitor": monitor}
    if command in ("previous", "next"):
        data.update(command="step", direction=1 if command == "next" else -1)
    elif command == "main":
        data.update(command="activate", page="main")
    elif command == "status":
        data.update(command="status")
    else:
        raise ValueError("Команда: previous, next, main или status")
    address = address or os.environ.get("OMD_CONTROL_SOCKET") or str(Path(os.environ["XDG_RUNTIME_DIR"])/"oh-my-desktop-pages.sock")
    with socket.socket(socket.AF_UNIX) as client:
        client.settimeout(2)
        client.connect(address)
        client.sendall(json.dumps(data).encode()+b"\n")
        reply = b""
        while b"\n" not in reply:
            part = client.recv(4096)
            if not part or len(reply) > 65536:
                raise ValueError("Некорректный ответ рабочего стола")
            reply += part
        result = json.loads(reply.split(b"\n",1)[0])
        if not result.get("ok"):
            raise ValueError("Монитор или страница недоступны")
        return result


def main():
    try:
        command = sys.argv[1] if len(sys.argv)>1 else "status"
        if len(sys.argv)>2:
            monitor = sys.argv[2]
        else:
            # The running desktop tracks focus through its existing IPC stream.
            # No extra compositor subprocess is needed on each wheel click.
            monitor = ""
        result = request(command,monitor)
        if command == "status":
            print(json.dumps(result,ensure_ascii=False))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print("Страницы рабочего стола: "+str(error),file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
