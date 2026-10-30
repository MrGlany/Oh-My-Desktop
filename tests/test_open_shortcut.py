import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("shortcut", Path(__file__).parents[1]/"src/open_shortcut.py")
shortcut = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shortcut)


class ShortcutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.target = self.root / "media" / "Windows 10" / "Folder ; $(no shell)"
        self.link = self.root / "Desktop link"
        self.link.symlink_to(self.target)
        self.device = dict(name="/dev/mock-only", label="Windows 10", uuid="test-uuid",
                           fstype="ntfs", mountpoints=[None])

    def media(self):
        return patch.object(shortcut, "media_target", return_value=(self.root/"media", "Windows 10", ("Folder ; $(no shell)",)))

    def test_existing_target_never_mounts(self):
        self.target.mkdir(parents=True)
        with patch.object(shortcut, "block_devices") as lsblk, patch.object(shortcut.subprocess, "run") as run:
            self.assertEqual(shortcut.resolve_shortcut(self.link), str(self.target))
            lsblk.assert_not_called()
            run.assert_not_called()

    def test_relative_link(self):
        self.link.unlink()
        self.target.mkdir(parents=True)
        self.link.symlink_to(os.path.relpath(self.target, self.root))
        self.assertEqual(shortcut.resolve_shortcut(self.link), str(self.target))

    def test_non_link_rejected(self):
        with self.assertRaises(shortcut.ShortcutError):
            shortcut.resolve_shortcut(self.root)

    def test_missing_non_media_target_never_mounts(self):
        with patch.object(shortcut.subprocess, "run") as run:
            with self.assertRaises(shortcut.ShortcutError):
                shortcut.resolve_shortcut(self.link)
            run.assert_not_called()

    def test_ambiguous_or_absent_device_never_mounts(self):
        for devices in ([], [self.device, self.device.copy()]):
            with self.media(), patch.object(shortcut, "block_devices", return_value=devices), patch.object(shortcut.subprocess, "run") as run:
                with self.assertRaises(shortcut.ShortcutError):
                    shortcut.resolve_shortcut(self.link)
                run.assert_not_called()

    def test_already_mounted_missing_file_never_remounts(self):
        self.device["mountpoints"] = [str(self.root/"media/Windows 10")]
        with self.media(), patch.object(shortcut, "block_devices", return_value=[self.device]), patch.object(shortcut.subprocess, "run") as run:
            with self.assertRaises(shortcut.ShortcutError):
                shortcut.resolve_shortcut(self.link)
            run.assert_not_called()

    def test_mount_authorization_failure_preserves_link(self):
        with self.media(), patch.object(shortcut, "block_devices", return_value=[self.device]), patch.object(shortcut.subprocess, "run", return_value=subprocess.CompletedProcess([],1,"","cancelled")):
            with self.assertRaisesRegex(shortcut.ShortcutError, "cancelled"):
                shortcut.resolve_shortcut(self.link)
        self.assertTrue(self.link.is_symlink())
        self.assertEqual(os.readlink(self.link), str(self.target))
        self.assertFalse(self.target.exists())

    def test_success_verifies_mount_and_does_not_modify_link(self):
        mounted = dict(self.device, mountpoints=[str(self.root/"media/Windows 10")])
        def mount(*args, **kwargs):
            self.assertEqual(args[0], ["/usr/bin/udisksctl", "mount", "--block-device", "/dev/mock-only"])
            self.assertNotIn("shell", kwargs)
            self.target.mkdir(parents=True)
            return subprocess.CompletedProcess([],0,"localized output ignored","")
        with self.media(), patch.object(shortcut, "block_devices", side_effect=[[self.device], [mounted]]), patch.object(shortcut.subprocess, "run", side_effect=mount):
            self.assertEqual(shortcut.resolve_shortcut(self.link), str(self.target))
        self.assertEqual(os.readlink(self.link), str(self.target))

    def test_unexpected_mount_path_rejected(self):
        mounted = dict(self.device, mountpoints=["/some/other/mount"])
        with self.media(), patch.object(shortcut, "block_devices", side_effect=[[self.device], [mounted]]), patch.object(shortcut.subprocess, "run", return_value=subprocess.CompletedProcess([],0,"","")):
            with self.assertRaisesRegex(shortcut.ShortcutError, "другому пути"):
                shortcut.resolve_shortcut(self.link)

    def test_changed_link_during_auth_rejected(self):
        mounted = dict(self.device, mountpoints=[str(self.root/"media/Windows 10")])
        def mount(*args, **kwargs):
            self.link.unlink()
            self.link.symlink_to(self.root/"changed")
            return subprocess.CompletedProcess([],0,"","")
        with self.media(), patch.object(shortcut, "block_devices", side_effect=[[self.device], [mounted]]), patch.object(shortcut.subprocess, "run", side_effect=mount):
            with self.assertRaisesRegex(shortcut.ShortcutError, "изменился"):
                shortcut.resolve_shortcut(self.link)

    def test_permission_denied_never_mounts(self):
        with patch.object(Path, "stat", side_effect=PermissionError), patch.object(shortcut.subprocess, "run") as run:
            with self.assertRaises(shortcut.ShortcutError):
                shortcut.accessible(self.target)
            run.assert_not_called()

    def test_media_prefix_component_boundary(self):
        user = shortcut.pwd.getpwuid(os.getuid()).pw_name
        for target in (f"/run/media/{user}-other/Windows 10/foo", "/mnt/Windows 10/foo"):
            with self.assertRaises(shortcut.ShortcutError):
                shortcut.media_target(Path(target))
        self.assertEqual(shortcut.media_target(Path(f"/run/media/{user}/Windows 10/foo"))[1], "Windows 10")

    def test_device_tree_parsing(self):
        result = subprocess.CompletedProcess([],0,'{"blockdevices":[{"name":"/dev/a","children":[{"name":"/dev/a1"}]}]}',"")
        with patch.object(shortcut.subprocess, "run", return_value=result):
            self.assertEqual([d["name"] for d in shortcut.block_devices()], ["/dev/a", "/dev/a1"])


if __name__ == "__main__":
    unittest.main()
