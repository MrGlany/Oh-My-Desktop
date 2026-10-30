"""Check immutable tag archives, checksums, metadata and refusal to overwrite."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipUnless(shutil.which('git') and shutil.which('makepkg'), 'requires Git and makepkg')
class Release(unittest.TestCase):
    def test_tag_is_immutable_and_metadata_matches(self):
        with tempfile.TemporaryDirectory(prefix='omd-release-test-') as tmp:
            base = Path(tmp)
            repo = base / 'repo'
            (repo / 'packaging/aur').mkdir(parents=True)
            shutil.copy(ROOT / 'packaging/prepare-release.py', repo / 'packaging/prepare-release.py')
            shutil.copy(ROOT / 'packaging/aur/PKGBUILD.in', repo / 'packaging/aur/PKGBUILD.in')
            (repo / 'CMakeLists.txt').write_text('project(OhMyDesktop VERSION 1.0.0 LANGUAGES CXX)\n')
            (repo / 'payload').write_text('committed source')
            def git(*args):
                return subprocess.run(['git', '-c', 'user.name=Release Test', '-c', 'user.email=test@example.invalid', *args], cwd=repo, check=True, capture_output=True)
            git('init', '-b', 'main')
            git('add', '.')
            git('-c', 'core.hooksPath=/dev/null', 'commit', '-m', 'fixture')
            git('tag', 'v1.0.0')
            git('tag', 'v9.9.9')
            (repo / 'payload').write_text('uncommitted work must not enter the release')
            def prepare(tag, output):
                return subprocess.run(['/usr/bin/python3', repo / 'packaging/prepare-release.py', '--ref', tag, '--output', output], capture_output=True, text=True)
            out = base / 'release'
            result = prepare('v1.0.0', out)
            self.assertEqual(result.returncode, 0, result.stderr)
            archive = out / 'oh-my-desktop-1.0.0.tar.xz'
            before = archive.read_bytes()
            digest = hashlib.sha256(before).hexdigest()
            self.assertIn(digest, (out / 'aur/PKGBUILD').read_text())
            self.assertIn(digest, (out / 'aur/.SRCINFO').read_text())
            with tarfile.open(archive) as tar:
                self.assertEqual(tar.extractfile('oh-my-desktop-1.0.0/payload').read(), b'committed source')
                self.assertFalse(any('/.git/' in name for name in tar.getnames()))
            self.assertNotEqual(prepare('v1.0.0', out).returncode, 0)
            self.assertEqual(archive.read_bytes(), before)
            invalid = base / 'wrong-version'
            self.assertNotEqual(prepare('v9.9.9', invalid).returncode, 0)
            self.assertFalse(invalid.exists())
            self.assertEqual((repo / 'payload').read_text(), 'uncommitted work must not enter the release')

if __name__ == '__main__':
    unittest.main()
