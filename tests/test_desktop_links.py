import importlib.util
import os
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('links', Path(__file__).parents[1]/'src/send_to_desktop.py')
m = importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Links(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.desktop = self.root/'Desktop';self.desktop.mkdir()
        self.sources = self.root/'originals';self.sources.mkdir()
    def tearDown(self):self.temp.cleanup()
    def test_target_captures_active_monitor_and_page(self):
        with patch.object(m,'desktop_call',return_value={'ok':True,'monitor':'HDMI-A-1','active':'work','desktop':str(self.desktop)}) as call:
            self.assertEqual(m.desktop_target(str(self.desktop)),{'monitor':'HDMI-A-1','page':'work'})
            call.assert_called_once_with({'command':'status','monitor':''})
    def test_other_desktop_profile_rejected(self):
        with patch.object(m,'desktop_call',return_value={'ok':True,'monitor':'DP-1','active':'main','desktop':str(self.root/'other')}):
            with self.assertRaises(ValueError):m.desktop_target(str(self.desktop))
    def test_placement_preserves_snapshot_after_focus_changes(self):
        created=[str(self.desktop/'one link'),str(self.desktop/'two link')]
        target={'monitor':'HDMI-A-1','page':'work'}
        with patch.object(m,'desktop_call',return_value={'ok':True}) as call:
            self.assertFalse(m.place_created(created,target))
            for args in call.call_args_list:
                self.assertEqual(args.args[0]['monitor'],'HDMI-A-1');self.assertEqual(args.args[0]['page'],'work')
                self.assertEqual(len(args.args[0]['names']),1)
    def test_app_stopped_does_not_create_links(self):
        src=self.sources/'data';src.write_text('keep')
        with patch.object(sys,'argv',['send-to-desktop',str(src)]),patch.object(m.subprocess,'check_output',return_value=str(self.desktop)+'\n'),patch.object(m,'desktop_target',side_effect=ConnectionRefusedError('not running')),patch.object(m,'notify'):
            self.assertEqual(m.main(),1)
        self.assertFalse(list(self.desktop.iterdir()));self.assertEqual(src.read_text(),'keep')
    def test_failed_placement_keeps_created_link_and_original(self):
        src=self.sources/'data';src.write_text('keep');created,errors=m.create_links([src],self.desktop)
        with patch.object(m,'desktop_call',side_effect=ConnectionRefusedError('stopped')):
            self.assertTrue(m.place_created(created,{'monitor':'DP-1','page':'work'}))
        self.assertTrue(Path(created[0]).is_symlink());self.assertEqual(src.read_text(),'keep')
    def test_folder(self):
        src=self.sources/'Папка с пробелами';src.mkdir();(src/'данные').write_text('keep')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);link=Path(created[0]);self.assertTrue(link.is_symlink())
        self.assertEqual(os.readlink(link),str(src));self.assertEqual((link/'данные').read_text(),'keep')
        link.unlink();self.assertEqual((src/'данные').read_text(),'keep')
    def test_desktop_uses_app_name(self):
        src=self.sources/'org.example.App.desktop';src.write_text('[Desktop Entry]\nName=Friendly App\n[Desktop Action quit]\nName=Quit\n')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertEqual(Path(created[0]).name,'Friendly App — ярлык.desktop')
    def test_file_keeps_extension(self):
        src=self.sources/'документ.txt';src.write_text('keep')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);link=Path(created[0]);self.assertEqual(link.suffix,'.txt');self.assertEqual(link.read_text(),'keep')
    def test_collisions_no_overwrite(self):
        src=self.sources/'file.txt';src.write_text('keep')
        a,_=m.create_links([src],self.desktop);b,_=m.create_links([src],self.desktop)
        self.assertNotEqual(a,b);self.assertTrue(Path(a[0]).is_symlink());self.assertTrue(Path(b[0]).is_symlink())
    def test_existing_file_preserved(self):
        src=self.sources/'file.txt';src.write_text('keep');dest=self.desktop/'file — ярлык.txt';dest.write_text('original')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertNotEqual(created[0],str(dest));self.assertEqual(dest.read_text(),'original')
    def test_dangling_target_not_overwritten(self):
        src=self.sources/'file.txt';src.write_text('keep');dest=self.desktop/'file — ярлык.txt';dest.symlink_to('/nonexistent')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertNotEqual(created[0],str(dest));self.assertEqual(os.readlink(dest),'/nonexistent')
    def test_shell_characters_are_literal(self):
        src=self.sources/'$(touch owned) `test` %F \'quoted\'.txt';src.write_text('keep')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertEqual(Path(created[0]).read_text(),'keep');self.assertFalse((self.root/'owned').exists())
    def test_mixed_selection(self):
        src=self.sources/'exists';src.write_text('keep')
        created,errors=m.create_links([src,self.sources/'missing'],self.desktop)
        self.assertEqual(len(created),1);self.assertEqual(len(errors),1);self.assertEqual(src.read_text(),'keep')
    def test_self_desktop_rejected(self):
        created,errors=m.create_links([self.desktop],self.desktop)
        self.assertFalse(created);self.assertTrue(errors)
    def test_special_file_rejected(self):
        src=self.sources/'fifo';os.mkfifo(src)
        created,errors=m.create_links([src],self.desktop);self.assertFalse(created);self.assertTrue(errors)
    def test_permission_error_preserves_source(self):
        src=self.sources/'file';src.write_text('keep')
        with patch.object(m.os,'symlink',side_effect=PermissionError('denied')):
            created,errors=m.create_links([src],self.desktop)
        self.assertFalse(created);self.assertTrue(errors);self.assertEqual(src.read_text(),'keep')
    def test_long_unicode_name(self):
        src=self.sources/('я'*100+'.txt');src.write_text('keep')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertLessEqual(len(os.fsencode(Path(created[0]).name)),255)
    def test_hidden_file_shortcut_visible(self):
        src=self.sources/'.hidden';src.write_text('keep')
        created,errors=m.create_links([src],self.desktop)
        self.assertFalse(errors);self.assertFalse(Path(created[0]).name.startswith('.'))

if __name__=='__main__':unittest.main(verbosity=2)
