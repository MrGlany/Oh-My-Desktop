"""Installer orchestration checks; pacman, sudo and package builds are mocked."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'install.sh'


class Installer(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='omd-installer-test-')
        self.base = Path(self.temp.name)
        self.repo = self.base / 'source with spaces'
        self.repo.mkdir()
        (self.repo / 'packaging/aur').mkdir(parents=True)
        shutil.copy2(SCRIPT, self.repo / 'install.sh')
        shutil.copy2(ROOT / 'packaging/prepare-release.py', self.repo / 'packaging/prepare-release.py')
        shutil.copy2(ROOT / 'packaging/aur/PKGBUILD.in', self.repo / 'packaging/aur/PKGBUILD.in')
        (self.repo / 'CMakeLists.txt').write_text('project(OhMyDesktop VERSION 1.2.3 LANGUAGES CXX)\n')
        for command in (['init', '-b', 'main'], ['add', '.'], ['-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', 'commit', '-m', 'fixture'], ['tag', 'v1.2.3']):
            subprocess.run(['git', *command], cwd=self.repo, check=True, capture_output=True)
        self.bin = self.base / 'bin';self.bin.mkdir()
        self.home = self.base / 'home';self.home.mkdir()
        self.scratch = self.base / 'scratch';self.scratch.mkdir()
        self.log = self.base / 'commands.log'
        self.tool('id', 'printf "%s\\n" "${MOCK_UID:-1000}"')
        self.tool('pacman', '''printf 'pacman %s\\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == -Q && "${MOCK_MISSING:-}" == "$2" ]]; then exit 1; fi
exit 0''')
        self.tool('sudo', 'printf "sudo %s\\n" "$*" >> "$MOCK_LOG"; exit 0')
        self.tool('makepkg', '''printf 'makepkg %s\\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == --printsrcinfo ]]; then printf 'pkgbase = fixture\\n'; exit 0; fi
exit "${MOCK_BUILD_STATUS:-0}"''')
        self.env = dict(os.environ, HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / '.config'), TMPDIR=str(self.scratch), MOCK_LOG=str(self.log), PATH=str(self.bin) + os.pathsep + os.environ['PATH'])

    def tearDown(self):
        self.temp.cleanup()

    def tool(self, name, body):
        path = self.bin / name;path.write_text('#!/usr/bin/env bash\n' + body + '\n');path.chmod(0o755)

    def call(self, *args, **env):
        return subprocess.run(['bash', self.repo / 'install.sh', *args], cwd=self.base, env=dict(self.env, **env), capture_output=True, text=True)

    def commands(self):
        return self.log.read_text() if self.log.exists() else ''

    def test_default_install_works_from_another_directory(self):
        result = self.call()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('makepkg --syncdeps --install', self.commands())
        self.assertNotIn('sudo ', self.commands())
        self.assertIn('Автозапуск не включён', result.stdout)
        self.assertIn('1.2.3', result.stdout)

    def test_build_only_never_installs(self):
        result = self.call('--build-only')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('--install', self.commands())
        self.assertNotIn('--syncdeps', self.commands())
        self.assertNotIn('sudo ', self.commands())

    def test_build_only_does_not_bootstrap_dependencies(self):
        result = self.call('--build-only', MOCK_MISSING='base-devel')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('sudo ', self.commands())
        self.assertNotIn('makepkg ', self.commands())

    def test_default_bootstraps_only_missing_tools(self):
        result = self.call(MOCK_MISSING='python')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('sudo pacman -S --needed -- python', self.commands())
        self.assertNotIn('-Syu', self.commands())

    def test_root_rejected_before_any_package_changes(self):
        result = self.call(MOCK_UID='0')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('sudo ', self.commands())
        self.assertNotIn('makepkg ', self.commands())

    def test_personal_install_preserved_and_no_packages_changed(self):
        path = self.home / '.local/bin/oh-my-desktop';path.parent.mkdir(parents=True);path.write_text('personal fixture')
        result = self.call()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_text(), 'personal fixture')
        self.assertEqual(self.commands(), '')

    def test_failure_stops_without_reporting_success(self):
        result = self.call(MOCK_BUILD_STATUS='17')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('Установлено.', result.stdout)
        self.assertIn('Установка остановлена', result.stderr)

    def test_version_argument_is_literal(self):
        marker = self.base / 'must-not-exist'
        result = self.call('--version', f'$(touch {marker})')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(marker.exists())
        self.assertNotIn('makepkg ', self.commands())

    def test_help_does_not_touch_packages(self):
        result = self.call('--help')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(self.commands(), '')


if __name__ == '__main__':
    unittest.main()
