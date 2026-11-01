#!/usr/bin/env bash
# Build a tagged release and let pacman manage the installation. No autostart edits.
set -euo pipefail

usage() {
    cat <<'EOF'
Установка Oh My Desktop для Arch Linux / CachyOS.
  ./install.sh                      Собрать и установить текущую версию из CMake
  ./install.sh --version 1.0.1      Выбрать конкретную версию
  ./install.sh --build-only         Только собрать, без установки зависимостей/пакета
  ./install.sh --help               Показать справку
Программа не запускается автоматически; конфиги Hyprland и Noctalia не меняются.
EOF
}

version=''
build_only=0
while (($#)); do
    case "$1" in
        --version)
            if (($# < 2)); then printf '%s\n' 'После --version нужен номер, например 1.0.1.' >&2; exit 1; fi
            version="$2"; shift 2 ;;
        --build-only) build_only=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'Неизвестный аргумент: %s\n' "$1" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ $(id -u) == 0 ]]; then
    printf '%s\n' 'Запустите ./install.sh обычным пользователем, без sudo.' >&2
    exit 1
fi
if ! command -v pacman >/dev/null; then
    printf '%s\n' 'Этот установщик предназначен для Arch Linux / CachyOS с pacman.' >&2
    exit 1
fi

source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ ! -f "$source_dir/packaging/prepare-release.py" || ! -f "$source_dir/CMakeLists.txt" ]]; then
    printf '%s\n' 'Не найдены исходники рядом с install.sh. Сначала клонируйте репозиторий.' >&2
    exit 1
fi

# Refuse a conflicting personal installation before changing any packages.
if (( ! build_only )); then
    config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
    for path in "$HOME/.local/bin/oh-my-desktop" \
                "$HOME/.local/lib/oh-my-desktop/oh-my-desktop" \
                "$HOME/.local/opt/oh-my-desktop/oh-my-desktop" \
                "$config_home/systemd/user/oh-my-desktop.service"; do
        if [[ -e "$path" || -L "$path" ]]; then
            printf 'Уже есть личная установка: %s\nСначала подготовьте переход на пакет. Ничего не изменено.\n' "$path" >&2
            exit 1
        fi
    done
fi

missing=()
for package in base-devel git python; do
    if ! pacman -Q "$package" >/dev/null 2>&1; then missing+=("$package"); fi
done
if ((${#missing[@]})); then
    if (( build_only )); then
        printf 'Не хватает инструментов: %s\nВ режиме --build-only зависимости не устанавливаются.\n' "${missing[*]}" >&2
        exit 1
    fi
    printf 'Понадобятся инструменты: %s\n' "${missing[*]}"
    sudo pacman -S --needed -- "${missing[@]}"
fi
for tool in git python3 makepkg; do
    if ! command -v "$tool" >/dev/null; then printf 'Не найдена команда: %s\n' "$tool" >&2; exit 1; fi
done
if [[ "$(git -C "$source_dir" rev-parse --show-toplevel)" != "$source_dir" ]]; then
    printf '%s\n' 'Нужна Git-копия репозитория, созданная через git clone.' >&2
    exit 1
fi
if [[ -z "$version" ]]; then
    version="$(python3 - "$source_dir/CMakeLists.txt" <<'PY'
from pathlib import Path
import re, sys
match=re.search(r'project\(OhMyDesktop VERSION (\d+\.\d+\.\d+) LANGUAGES CXX\)',Path(sys.argv[1]).read_text())
if not match:sys.exit('Не удалось прочитать версию CMake.')
print(match[1])
PY
)"
fi
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    printf '%s\n' 'Номер версии должен выглядеть как 1.0.1, без буквы v.' >&2
    exit 1
fi
if ! git -C "$source_dir" show-ref --verify --quiet "refs/tags/v$version"; then
    printf 'Получаем тег v%s…\n' "$version"
    git -C "$source_dir" fetch --no-tags origin "refs/tags/v$version:refs/tags/v$version"
fi

build_dir="$(mktemp -d "${TMPDIR:-/tmp}/oh-my-desktop-install.XXXXXX")"
printf '\nOh My Desktop %s\nПапка сборки: %s\n' "$version" "$build_dir"
trap 'printf "Установка остановлена. Результаты сборки сохранены: %s\n" "$build_dir" >&2' ERR
python3 "$source_dir/packaging/prepare-release.py" --ref "v$version" --output "$build_dir/release"
cp -- "$build_dir/release/oh-my-desktop-$version.tar.xz" "$build_dir/release/aur/"
cd -- "$build_dir/release/aur"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
if (( build_only )); then
    makepkg
else
    makepkg --syncdeps --install
fi
trap - ERR
if (( build_only )); then
    printf '\nПакет собран без установки: %s/release/aur/\n' "$build_dir"
else
    printf '\nУстановлено. Запуск в сеансе Hyprland: /usr/bin/oh-my-desktop\n'
    printf 'Автозапуск не включён. Перед первым запуском сохраните резервную копию файлов рабочего стола.\n'
fi
printf 'Файлы сборки сохранены: %s\n' "$build_dir"
