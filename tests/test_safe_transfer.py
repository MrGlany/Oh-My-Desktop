import errno
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('safe_transfer', Path(__file__).resolve().parents[1] / 'src/safe_transfer.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Safety(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.src = self.root / 'source'
        self.dst = self.root / 'target'
    def tearDown(self):
        self.temp.cleanup()
    def tree(self):
        self.src.mkdir()
        (self.src / '.hidden').write_bytes(b'secret\x00')
        (self.src / '.folder').mkdir()
        (self.src / '.folder' / '.nested').write_text('nested')
        (self.src / 'normal').write_text('normal')
        (self.src / 'dangling').symlink_to('missing')
        (self.src / 'loop').symlink_to('.')
        os.chmod(self.src / 'normal', 0o640)
        os.setxattr(self.src / 'normal', 'user.omd-test', b'metadata')
    def test_copy_hidden_symlinks_metadata(self):
        self.tree(); expected=m.snapshot(self.src)
        m.transfer(self.src,self.dst)
        self.assertEqual(m.snapshot(self.dst),expected)
        self.assertEqual(m.snapshot(self.src),expected)
    def test_same_device_move(self):
        self.tree(); expected=m.snapshot(self.src)
        m.transfer(self.src,self.dst,move=True)
        self.assertFalse(self.src.exists()); self.assertEqual(m.snapshot(self.dst),expected)
    def test_cross_device_move(self):
        self.tree(); expected=m.snapshot(self.src)
        # /dev/shm and /tmp are distinct tmpfs mounts on this machine.
        with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='omd-test-') as other:
            if os.stat(other).st_dev == os.stat(self.root).st_dev:
                self.fail('Cross-device fixture requires distinct filesystems')
            dest=Path(other)/'target'
            result=m.transfer(self.src,dest,move=True)
            self.assertEqual(m.snapshot(dest),expected)
            self.assertFalse(self.src.exists())
            self.assertEqual(m.snapshot(Path(result['recovery'][0])),expected)
    def test_replacement_retains_old_directory(self):
        self.tree(); self.dst.mkdir(); (self.dst/'.old').write_text('old')
        old=m.snapshot(self.dst)
        result=m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(m.snapshot(Path(result['recovery'][0])),old)
        self.assertEqual(m.snapshot(self.src),m.snapshot(self.dst))
    def test_replacement_retains_old_file(self):
        self.src.write_text('new'); self.dst.write_text('old')
        result=m.transfer(self.src,self.dst,move=True,replace=True)
        self.assertEqual(self.dst.read_text(),'new')
        self.assertEqual(Path(result['recovery'][0]).read_text(),'old')
    def test_no_clobber(self):
        self.src.write_text('new'); self.dst.write_text('old')
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst)
        self.assertEqual(self.dst.read_text(),'old'); self.assertEqual(self.src.read_text(),'new')
    def test_same_path(self):
        self.tree()
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.src,move=True,replace=True)
        self.assertTrue((self.src/'.hidden').exists())
    def test_hardlink_alias(self):
        self.src.write_text('data'); os.link(self.src,self.dst)
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,move=True,replace=True)
        self.assertEqual(self.src.read_text(),'data')
    def test_destination_inside_source(self):
        self.tree()
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.src/'child')
    def test_destination_contains_source(self):
        self.tree()
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.root,move=True,replace=True)
    def test_parent_symlink(self):
        self.tree(); alias=self.root/'alias'; alias.symlink_to(self.src,target_is_directory=True)
        with self.assertRaises(m.TransferError): m.transfer(self.src,alias/'new')
    def test_missing_source(self):
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst)
    def test_special_file_rejected(self):
        self.tree(); os.mkfifo(self.src/'fifo')
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,move=True)
        self.assertTrue((self.src/'.hidden').exists()); self.assertFalse(self.dst.exists())
    def test_copy_error_preserves_originals(self):
        self.tree(); self.dst.write_text('old')
        with patch.object(m.shutil,'copytree',side_effect=OSError(errno.ENOSPC,'full')):
            with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,move=False,replace=True)
        self.assertEqual(self.dst.read_text(),'old'); self.assertTrue((self.src/'.hidden').exists())
    def test_partial_copy_preserves_originals(self):
        self.tree(); self.dst.write_text('old')
        def broken(src,dst,**kwargs):
            dst.mkdir(); (dst/'partial').write_text('partial'); raise OSError('disk failure')
        with patch.object(m.shutil,'copytree',side_effect=broken):
            with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.dst.read_text(),'old'); self.assertTrue((self.src/'.hidden').exists())
    def test_corrupt_copy_rejected(self):
        self.src.write_text('source'); self.dst.write_text('old')
        def corrupt(src,dst,**kwargs): dst.write_text('corrupt')
        with patch.object(m.shutil,'copy2',side_effect=corrupt):
            with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.src.read_text(),'source'); self.assertEqual(self.dst.read_text(),'old')
    def test_source_changes_during_copy(self):
        self.src.write_text('source'); self.dst.write_text('old'); real=m.shutil.copy2
        def changed(src,dst,**kwargs):
            real(src,dst,**kwargs); src.write_text('changed')
        with patch.object(m.shutil,'copy2',side_effect=changed):
            with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.src.read_text(),'changed'); self.assertEqual(self.dst.read_text(),'old')
    def test_commit_failure_rolls_back(self):
        self.src.write_text('new'); self.dst.write_text('old'); real=m.rename_new
        def fail_commit(src,dst):
            if Path(src).name=='payload': raise OSError('injected commit failure')
            real(src,dst)
        with patch.object(m,'rename_new',side_effect=fail_commit):
            with self.assertRaises(OSError): m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.dst.read_text(),'old'); self.assertEqual(self.src.read_text(),'new')
    def test_racing_destination_not_overwritten(self):
        self.src.write_text('new'); real=m.rename_new
        def race(src,dst):
            self.dst.write_text('racer'); real(src,dst)
        with patch.object(m,'rename_new',side_effect=race):
            with self.assertRaises(FileExistsError): m.transfer(self.src,self.dst)
        self.assertEqual(self.dst.read_text(),'racer'); self.assertEqual(self.src.read_text(),'new')
    def test_rollback_race_preserves_all(self):
        self.src.write_text('new'); self.dst.write_text('old'); real=m.rename_new
        def race(src,dst):
            if Path(src).name=='payload': self.dst.write_text('racer')
            real(src,dst)
        with patch.object(m,'rename_new',side_effect=race):
            with self.assertRaises(FileExistsError): m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.dst.read_text(),'racer'); self.assertEqual(self.src.read_text(),'new')
        self.assertEqual(next(self.root.glob('.omd-recovery-*/target')).read_text(),'old')
    def test_marker_filename_is_literal(self):
        self.src.write_text('new'); dest=self.root/'literal__REPLACE__'
        m.transfer(self.src,dest); self.assertEqual(dest.read_text(),'new')
    def test_cross_device_failure_keeps_source_and_target(self):
        self.tree(); expected=m.snapshot(self.src)
        with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='omd-test-') as other:
            dest=Path(other)/'target'; dest.write_text('old')
            with patch.object(m.shutil,'copytree',side_effect=OSError(errno.ENOSPC,'full')):
                with self.assertRaises(m.TransferError): m.transfer(self.src,dest,move=True,replace=True)
            self.assertEqual(m.snapshot(self.src),expected); self.assertEqual(dest.read_text(),'old')
    def test_replace_dangling_symlink_keeps_link_backup(self):
        self.src.write_text('new'); self.dst.symlink_to('missing')
        result=m.transfer(self.src,self.dst,replace=True)
        self.assertEqual(self.dst.read_text(),'new')
        backup=Path(result['recovery'][0]); self.assertTrue(backup.is_symlink())
        self.assertEqual(os.readlink(backup),'missing')
    def test_dangling_source_copy(self):
        self.src.symlink_to('missing'); m.transfer(self.src,self.dst)
        self.assertEqual(os.readlink(self.dst),'missing'); self.assertTrue(self.src.is_symlink())
    def test_cross_device_source_changes_after_commit(self):
        self.src.write_text('before'); real=m.rename_new
        with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='omd-test-') as other:
            dest=Path(other)/'target'
            def changed(src,dst):
                real(src,dst)
                if Path(dst)==dest: self.src.write_text('concurrent edit')
            with patch.object(m,'rename_new',side_effect=changed): result=m.transfer(self.src,dest,move=True)
            self.assertEqual(self.src.read_text(),'concurrent edit'); self.assertEqual(dest.read_text(),'before')
            self.assertIn('Source retained',result['recovery'][0])
    def test_dangling_destination_not_overwritten(self):
        self.src.write_text('new'); self.dst.symlink_to('missing')
        with self.assertRaises(m.TransferError): m.transfer(self.src,self.dst)
        self.assertTrue(self.dst.is_symlink())

if __name__=='__main__': unittest.main(verbosity=2)
