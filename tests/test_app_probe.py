import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('app_probe',Path(__file__).resolve().parents[1]/'src/app_probe.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)

class Availability(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.file=self.root/'Fixture.desktop'
    def tearDown(self):self.tmp.cleanup()
    def check(self,command,extra=''):
        self.file.write_text('[Desktop Entry]\nType=Application\nName=Fixture\nExec='+command+'\n'+extra)
        return probe.probe(self.file)['status']
    def test_missing(self):self.assertEqual(self.check('/nonexistent-omd-fixture/bin/app'),'missing')
    def test_available(self):self.assertEqual(self.check('/usr/bin/true %U'),'available')
    def test_path_lookup(self):self.assertEqual(self.check('true'),'available')
    def test_missing_path_lookup(self):self.assertEqual(self.check('omd-no-such-fixture-command-92841'),'missing')
    def test_spaces(self):
        p=self.root/'app with spaces';p.write_text('#!/bin/sh\nexit 0\n');p.chmod(0o700)
        self.assertEqual(self.check('"'+str(p)+'" %F'),'available')
    def test_env(self):self.assertEqual(self.check('env FOO=bar /nonexistent-omd-fixture/bin/app'),'missing')
    def test_env_path_uncertain(self):self.assertEqual(self.check('env PATH=/different true'),'unknown')
    def test_script_uncertain(self):self.assertEqual(self.check('sh -c "no-such-inner-app"'),'unknown')
    def test_shell_compound_uncertain(self):self.assertEqual(self.check('false || true'),'unknown')
    def test_try_exec(self):self.assertEqual(self.check('true','TryExec=omd-no-such-fixture-command-92841\n'),'missing')
    def test_external_volume(self):self.assertEqual(self.check('/run/media/coder/absent-volume/app'),'unavailable')
    def test_path_permission_preserves(self):
        p=self.root/'no-permission';p.touch()
        with patch.dict(os.environ,{'PATH':str(self.root)}):
            self.assertEqual(self.check('no-permission'),'unavailable')
    def test_uncertain_try_exec(self):self.assertEqual(self.check('true','TryExec="app with spaces"\n'),'unknown')
    def test_no_exec_permission(self):
        p=self.root/'app';p.touch();self.assertEqual(self.check(str(p)),'unavailable')
    def test_never_executes_application(self):
        marker=self.root/'executed';p=self.root/'app';p.write_text('#!/bin/sh\ntouch "'+str(marker)+'"\n');p.chmod(0o700)
        self.assertEqual(self.check(str(p)),'available');self.assertFalse(marker.exists())
    def test_sections(self):self.assertEqual(self.check('true','[Desktop Action Other]\nExec=does-not-exist\n'),'available')
    def test_invalid(self):self.assertEqual(self.check('"unterminated'),'unknown')
    def test_missing_shortcut(self):self.assertEqual(probe.probe(self.file)['status'],'unknown')
    def test_link_type(self):
        self.file.write_text('[Desktop Entry]\nType=Link\nURL=file:///absent\n');self.assertEqual(probe.probe(self.file)['status'],'unknown')
    @patch.object(probe.os.path,'isfile',return_value=True)
    @patch.object(probe.subprocess,'run')
    def test_flatpak_missing(self,run,_):
        run.return_value=subprocess.CompletedProcess([],1,'','error: app/org.example.Removed is not installed')
        self.assertEqual(probe.flatpak_status(['run','--branch=stable','--arch=x86_64','--command=app','--file-forwarding','org.example.Removed','@@u','%U','@@']),'missing')
        self.assertEqual(run.call_count,2)
        for call in run.call_args_list:self.assertIn('info',call.args[0]);self.assertNotIn('run',call.args[0])
    @patch.object(probe.os.path,'isfile',return_value=True)
    @patch.object(probe.subprocess,'run')
    def test_flatpak_other_installation(self,run,_):
        run.side_effect=[subprocess.CompletedProcess([],1,'','not installed'),subprocess.CompletedProcess([],0,'app/org.example.App/x86_64/stable','')]
        self.assertEqual(probe.flatpak_status(['run','org.example.App']),'available')
    @patch.object(probe.os.path,'isfile',return_value=True)
    @patch.object(probe.subprocess,'run')
    def test_flatpak_error_preserves(self,run,_):
        run.return_value=subprocess.CompletedProcess([],1,'','Permission denied')
        self.assertEqual(probe.flatpak_status(['run','org.example.App']),'unknown')
    @patch.object(probe.os.path,'isfile',return_value=True)
    @patch.object(probe.subprocess,'run')
    def test_flatpak_timeout_preserves(self,run,_):
        run.side_effect=subprocess.TimeoutExpired([],2)
        self.assertEqual(probe.flatpak_status(['run','org.example.App']),'unknown')
    @patch.object(probe.os.path,'isfile',return_value=True)
    @patch.object(probe.subprocess,'run')
    def test_flatpak_scope_branch_arch(self,run,_):
        run.return_value=subprocess.CompletedProcess([],1,'','not installed')
        self.assertEqual(probe.flatpak_status(['--user','run','--branch=beta','--arch=x86_64','org.example.App']),'missing')
        run.assert_called_once()
        self.assertEqual(run.call_args.args[0],['/usr/bin/flatpak','--user','info','--show-ref','--arch=x86_64','org.example.App','beta'])
    @patch.object(probe.subprocess,'run')
    def test_flatpak_bad_identifier(self,run):
        self.assertEqual(probe.flatpak_status(['run','--bad','org.example.App']),'unknown');run.assert_not_called()

if __name__=='__main__':unittest.main()
