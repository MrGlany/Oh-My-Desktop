import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('application_icon',Path(__file__).resolve().parents[1]/'src/application_icon.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Discovery(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.bundle=self.root/'bundle';self.bundle.mkdir()
        self.binary=self.bundle/'Fixture App';self.binary.write_bytes(b'\x7fELF fixture');self.binary.chmod(0o700)
        self.cache=self.root/'cache'
        self.env=patch.dict(os.environ,{'XDG_DATA_HOME':str(self.root/'data'),'XDG_DATA_DIRS':str(self.root/'global')});self.env.start()
    def tearDown(self):self.env.stop();self.temp.cleanup()
    def icon(self,root,name='org.fixture.App',size='1024x1024'):
        p=root/'share/icons/hicolor'/size/'apps'/(name+'.png');p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'png fixture');return p
    def desktop(self,root,exe='fixture-app',name='Fixture App',icon='org.fixture.App'):
        p=root/'share/applications/org.fixture.App.desktop';p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text('[Desktop Entry]\nType=Application\nName='+name+'\nExec='+exe+'\nIcon='+icon+'\n');return p
    def discover(self,p=None):return m.discover(p or self.binary,self.cache)
    def test_flutter_bundle_metadata(self):
        expected=self.icon(self.bundle);self.desktop(self.bundle)
        self.assertEqual(self.discover()['path'],str(expected))
    def test_prefers_large_icon(self):
        self.icon(self.bundle,size='16x16');expected=self.icon(self.bundle);self.desktop(self.bundle)
        self.assertEqual(self.discover()['path'],str(expected))
    def test_symlink_to_binary(self):
        expected=self.icon(self.bundle);self.desktop(self.bundle);link=self.root/'shortcut';link.symlink_to(self.binary)
        self.assertEqual(self.discover(link)['path'],str(expected));self.assertTrue(link.is_symlink())
    def test_exact_installed_exec(self):
        root=self.root/'install';expected=self.icon(root);d=self.desktop(root,exe='"'+str(self.binary)+'"')
        destination=self.root/'data/applications';destination.mkdir(parents=True);d.rename(destination/d.name)
        icons=root/'share/icons';icons.rename(self.root/'data/icons')
        self.assertEqual(self.discover()['path'],str(self.root/'data/icons/hicolor/1024x1024/apps/org.fixture.App.png'))
    def test_unrelated_metadata_not_used(self):
        self.desktop(self.bundle,exe='unrelated',name='Other App');self.assertEqual(self.discover(),{})
    def test_sidecar(self):
        p=self.bundle/'Fixture App.png';p.write_bytes(b'png');self.assertEqual(self.discover()['path'],str(p))
    def test_desktop_absolute_icon(self):
        p=self.icon(self.bundle);d=self.desktop(self.bundle,icon=str(p));self.assertEqual(self.discover(d)['path'],str(p))
    def test_desktop_theme_icon(self):
        d=self.desktop(self.bundle,icon='fixture-theme');self.assertEqual(self.discover(d)['theme'],'fixture-theme')
    def test_missing_or_directory(self):
        self.assertEqual(self.discover(self.root/'missing'),{});self.assertEqual(self.discover(self.bundle),{})
    def test_nonexecutables_not_scanned(self):
        self.binary.chmod(0o600);self.desktop(self.bundle);self.icon(self.bundle);self.assertEqual(self.discover(),{})
    def test_never_execute_application(self):
        marker=self.root/'executed';self.binary.write_text('#!/bin/sh\ntouch "'+str(marker)+'"\n')
        self.discover();self.assertFalse(marker.exists())
    def test_env_metadata(self):
        self.icon(self.bundle);self.desktop(self.bundle,exe='env FOO=bar "'+str(self.binary)+'"',name='Different');self.assertEqual(self.discover()['source'],'bundle')
    def test_bad_image_size(self):
        p=self.bundle/'icon.png'
        with p.open('wb') as f:f.truncate(m.MAX_IMAGE+1)
        self.assertFalse(m.image_file(p))
    def test_archive_entry_traversal(self):
        self.binary.write_bytes(b'\x7fELF'+b'\0'*4+b'AI\x02'+b'\0'*20+b'hsqs')
        with patch.object(m.shutil,'which',return_value='/usr/bin/unsquashfs'),patch.object(m,'run_tool',side_effect=[b'',b'lrwx squashfs-root/.DirIcon -> ../../outside.png']):
            self.assertIsNone(m.embedded(self.binary,self.cache))
        self.assertFalse(self.cache.exists())
    def test_appimage_resource_read_without_execution(self):
        self.binary.write_bytes(b'\x7fELF'+b'\0'*4+b'AI\x02'+b'\0'*20+b'hsqs')
        with patch.object(m.shutil,'which',return_value='/usr/bin/unsquashfs'),patch.object(m,'run_tool',return_value=b'png fixture') as tool:
            result=m.embedded(self.binary,self.cache)
            self.assertEqual(result['source'],'embedded');self.assertEqual(Path(result['path']).read_bytes(),b'png fixture')
            self.assertEqual(tool.call_args.args[0][0],'/usr/bin/unsquashfs');self.assertIn('-cat',tool.call_args.args[0])
    def test_pe_resource_read_without_execution(self):
        pe=self.bundle/'fixture.exe';pe.write_bytes(b'MZ fixture')
        with patch.object(m.shutil,'which',return_value='/usr/bin/wrestool'),patch.object(m,'run_tool',return_value=b'ico fixture') as tool:
            result=m.embedded(pe,self.cache);self.assertEqual(Path(result['path']).suffix,'.ico')
            self.assertEqual(tool.call_args.args[0][0],'/usr/bin/wrestool')
        self.assertEqual(pe.read_bytes(),b'MZ fixture')
    def test_missing_optional_extractor(self):
        pe=self.bundle/'fixture.exe';pe.write_bytes(b'MZ')
        with patch.object(m.shutil,'which',return_value=None):self.assertEqual(self.discover(pe),{})

if __name__=='__main__':unittest.main()
