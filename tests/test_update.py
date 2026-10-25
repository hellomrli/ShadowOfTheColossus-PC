"""Launcher self-update (launcher/sotc_update.py) with a local test release."""
from paths import ROOT
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
import zipfile

sys.path.insert(0, str(ROOT / 'launcher'))
import sotc_update  # noqa: E402


def release_zip(path, files):
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in files.items():
            archive.writestr(name, data)


class VersionTests(unittest.TestCase):
    def test_order(self):
        key = sotc_update.version_key
        self.assertGreater(key('0.57-opt6'), key('0.57-opt5'))
        self.assertGreater(key('0.58'), key('0.57-opt9'))
        self.assertGreater(key('v0.58-opt1'), key('0.58'))
        self.assertEqual(key('0.57-opt5'), key('v0.57-opt5'))

    def test_is_newer(self):
        self.assertTrue(sotc_update.is_newer({'version': '0.57-opt6'}, '0.57-opt5'))
        self.assertFalse(sotc_update.is_newer({'version': '0.57-opt5'}, '0.57-opt5'))
        self.assertFalse(sotc_update.is_newer(None, '0.57-opt5'))


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.old_url = os.environ.get('SOTC_UPDATE_URL')
        self.addCleanup(self.restore_url)

    def restore_url(self):
        if self.old_url is None:
            os.environ.pop('SOTC_UPDATE_URL', None)
        else:
            os.environ['SOTC_UPDATE_URL'] = self.old_url

    def publish(self, files, digest=None, size=None):
        zip_path = self.dir / 'SotC-PC.zip'
        release_zip(zip_path, files)
        data = zip_path.read_bytes()
        meta = {'tag_name': 'v9.99-opt1', 'name': 'test', 'body': 'notes', 'html_url': 'about:blank',
                'assets': [{'name': 'SotC-PC.zip', 'browser_download_url': zip_path.as_uri(),
                            'size': len(data) if size is None else size,
                            'digest': 'sha256:' + (digest or hashlib.sha256(data).hexdigest())}]}
        (self.dir / 'latest.json').write_text(json.dumps(meta))
        os.environ['SOTC_UPDATE_URL'] = (self.dir / 'latest.json').as_uri()
        return sotc_update.latest_release()

    def test_default_release_feed_stays_on_this_fork(self):
        with patch.dict(os.environ, {'SOTC_UPDATE_URL': ''}), \
                patch.object(sotc_update, '_open', return_value=BytesIO(b'{}')) as fetch:
            self.assertIsNone(sotc_update.latest_release())
        fetch.assert_called_once_with(
            'https://api.github.com/repos/hellomrli/ShadowOfTheColossus-PC/releases/latest', 10)

    def test_good_release(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe', 'SotC-PC/bin/a.dll': b'dll'})
        self.assertEqual(release['version'], '9.99-opt1')
        new = sotc_update.download(release, self.dir / 'update')
        self.assertEqual((new / 'bin' / 'a.dll').read_bytes(), b'dll')
        self.assertFalse((self.dir / 'update' / 'SotC-PC.zip.part').exists())

    def test_bad_digest(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe'}, digest='0' * 64)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            sotc_update.download(release, self.dir / 'update')

    def test_bad_size(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe'}, size=1)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            sotc_update.download(release, self.dir / 'update')

    def test_wrong_layout(self):
        release = self.publish({'Other/SotC.exe': b'exe'})
        with self.assertRaisesRegex(ValueError, 'not a SotC-PC release'):
            sotc_update.download(release, self.dir / 'update')

    def test_path_escape(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe', 'SotC-PC/../evil.txt': b'x'})
        with self.assertRaisesRegex(ValueError, 'unexpected path'):
            sotc_update.download(release, self.dir / 'update')

    def test_windows_path_aliases_are_rejected_on_every_host(self):
        for name in ('SotC-PC/..\\evil.txt', 'SotC-PC/user./save.dat',
                     'SotC-PC/user /save.dat', 'SotC-PC/bin/a.dll:stream',
                     'SotC-PC/bin/NUL.txt', 'SotC-PC/bin/COM1',
                     'SotC-PC//bin/a.dll', 'SotC-PC/./bin/a.dll'):
            with self.subTest(name=name):
                release = self.publish({'SotC-PC/SotC.exe': b'exe', name: b'x'})
                with self.assertRaisesRegex(ValueError, 'unexpected path'):
                    sotc_update.download(release, self.dir / 'update')

    def test_case_insensitive_duplicate_paths_are_rejected(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe',
                                'SotC-PC/bin/a.dll': b'first',
                                'SotC-PC/BIN/A.DLL': b'second'})
        with self.assertRaisesRegex(ValueError, 'duplicate path'):
            sotc_update.download(release, self.dir / 'update')

    def test_directory_entries_are_supported(self):
        release = self.publish({'SotC-PC/': b'', 'SotC-PC/bin/': b'',
                                'SotC-PC/SotC.exe': b'exe', 'SotC-PC/bin/a.dll': b'dll'})
        new = sotc_update.download(release, self.dir / 'update')
        self.assertEqual((new / 'bin' / 'a.dll').read_bytes(), b'dll')

    def test_archive_symlinks_are_rejected(self):
        link = zipfile.ZipInfo('SotC-PC/bin/link')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        release = self.publish({'SotC-PC/SotC.exe': b'exe', link: b'../../user'})
        with self.assertRaisesRegex(ValueError, 'symlink'):
            sotc_update.download(release, self.dir / 'update')

    def test_download_does_not_replace_an_unrecovered_update(self):
        release = self.publish({'SotC-PC/SotC.exe': b'exe'})
        update = self.dir / 'update'
        update.mkdir()
        (update / 'recovery-needed').write_text('unfinished rollback')
        with self.assertRaisesRegex(RuntimeError, 'needs recovery'):
            sotc_update.download(release, update)


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.port, self.new = root / 'port', root / 'new' / 'SotC-PC'
        for base, files in ((self.port, {'SotC.exe': b'old exe', 'bin/a.dll': b'old dll', 'sotc.ini': b'mine',
                                         'user/save.dat': b'save', 'extra.txt': b'kept'}),
                            (self.new, {'SotC.exe': b'new exe', 'bin/a.dll': b'new dll', 'bin/b.dll': b'added',
                                        'sotc.ini': b'default', 'user/save.dat': b'overwritten?'})):
            for name, data in files.items():
                path = base / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)

    def test_apply_keeps_user_files(self):
        self.assertTrue(sotc_update.apply_update(self.new, self.port, restart=False))
        read = lambda name: (self.port / name).read_bytes()  # noqa: E731
        self.assertEqual(read('SotC.exe'), b'new exe')
        self.assertEqual(read('bin/a.dll'), b'new dll')
        self.assertEqual(read('bin/b.dll'), b'added')
        self.assertEqual(read('sotc.ini'), b'mine')
        self.assertEqual(read('user/save.dat'), b'save')
        self.assertEqual(read('extra.txt'), b'kept')
        self.assertIsNone(sotc_update.cleanup(self.port))
        self.assertFalse((self.port / 'update' / 'backup').exists())

    def test_failure_restores_everything(self):
        # bin/c.dll cannot be written (a folder of that name is in the way) after SotC.exe and
        # bin/a.dll were already replaced: both come back and the added bin/b.dll goes.
        (self.new / 'bin' / 'c.dll').write_bytes(b'blocked')
        (self.port / 'bin' / 'c.dll').mkdir()
        self.assertFalse(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertEqual((self.port / 'SotC.exe').read_bytes(), b'old exe')
        self.assertEqual((self.port / 'bin' / 'a.dll').read_bytes(), b'old dll')
        self.assertFalse((self.port / 'bin' / 'b.dll').exists())
        self.assertIn('', sotc_update.cleanup(self.port))  # the error is reported once
        self.assertIsNone(sotc_update.cleanup(self.port))

    def test_protected_names_are_case_insensitive(self):
        (self.new / 'USER').mkdir(exist_ok=True)
        (self.new / 'USER' / 'save.dat').write_bytes(b'not my save')
        (self.new / 'UPDATE').mkdir()
        (self.new / 'UPDATE' / 'injected.txt').write_bytes(b'not updater state')
        (self.new / 'SOTC.INI').write_bytes(b'not my settings')
        self.assertTrue(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertEqual((self.port / 'user' / 'save.dat').read_bytes(), b'save')
        self.assertEqual((self.port / 'sotc.ini').read_bytes(), b'mine')
        self.assertFalse((self.port / 'UPDATE' / 'injected.txt').exists())
        if os.name != 'nt':
            self.assertFalse((self.port / 'USER').exists())
            self.assertFalse((self.port / 'SOTC.INI').exists())

    def symlink(self, path, target, directory=False):
        try:
            path.symlink_to(target, target_is_directory=directory)
        except OSError as error:
            self.skipTest(f'symlinks not available: {error}')

    def test_destination_symlink_cannot_overwrite_saves(self):
        link = self.port / 'bin' / 'b.dll'
        self.symlink(link, self.port / 'user' / 'save.dat')
        self.assertFalse(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertEqual((self.port / 'user' / 'save.dat').read_bytes(), b'save')
        self.assertTrue(link.is_symlink())
        self.assertEqual((self.port / 'SotC.exe').read_bytes(), b'old exe')

    def test_source_symlink_is_rejected(self):
        src = self.new / 'bin' / 'b.dll'
        src.unlink()
        self.symlink(src, self.port / 'user' / 'save.dat')
        self.assertFalse(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertFalse((self.port / 'bin' / 'b.dll').exists())

    def test_destination_directory_link_cannot_overwrite_saves(self):
        (self.new / 'assets').mkdir()
        (self.new / 'assets' / 'save.dat').write_bytes(b'not my save')
        self.symlink(self.port / 'assets', self.port / 'user', directory=True)
        self.assertFalse(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertEqual((self.port / 'user' / 'save.dat').read_bytes(), b'save')

    def test_cleanup_does_not_follow_an_update_directory_link(self):
        self.symlink(self.port / 'update', self.port / 'user', directory=True)
        self.assertIn('link', sotc_update.cleanup(self.port))
        self.assertEqual((self.port / 'user' / 'save.dat').read_bytes(), b'save')

    def test_incomplete_rollback_keeps_backups_and_blocks_another_update(self):
        copy = sotc_update._copy

        def fail_copy(src, dst):
            if src == self.new / 'bin' / 'b.dll' or 'backup' in src.parts:
                raise OSError('file locked during restore')
            copy(src, dst)

        with patch.object(sotc_update, '_copy', side_effect=fail_copy), \
                patch.object(sotc_update.subprocess, 'Popen') as restart:
            self.assertFalse(sotc_update.apply_update(self.new, self.port))
        restart.assert_not_called()
        backup = self.port / 'update' / 'backup'
        self.assertEqual((backup / 'SotC.exe').read_bytes(), b'old exe')
        self.assertIn('backup', sotc_update.cleanup(self.port))
        self.assertIn('backup', sotc_update.cleanup(self.port))
        self.assertEqual((backup / 'SotC.exe').read_bytes(), b'old exe')
        self.assertFalse(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertEqual((backup / 'SotC.exe').read_bytes(), b'old exe')

    def test_interrupted_update_keeps_backups(self):
        with patch.object(sotc_update, '_copy', side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                sotc_update.apply_update(self.new, self.port, restart=False)
        self.assertIn('backup', sotc_update.cleanup(self.port))
        self.assertEqual((self.port / 'update' / 'backup' / 'SotC.exe').read_bytes(), b'old exe')

    def test_cleanup_keeps_inputs_of_an_incomplete_update(self):
        update = self.port / 'update'
        (update / 'new').mkdir(parents=True)
        (update / 'new' / 'input.txt').write_text('needed by the applier')
        (update / 'recovery-needed').write_text('applier may still be running')
        self.assertIn('backup', sotc_update.cleanup(self.port))
        self.assertTrue((update / 'new' / 'input.txt').exists())

    def test_wait_failure_does_not_replace_files(self):
        with patch.object(sotc_update, '_wait_for_exit', side_effect=TimeoutError('still running')), \
                patch.object(sotc_update.subprocess, 'Popen') as restart:
            self.assertFalse(sotc_update.apply_update(self.new, self.port, pid=123))
        restart.assert_not_called()
        self.assertEqual((self.port / 'SotC.exe').read_bytes(), b'old exe')
        self.assertFalse((self.port / 'bin' / 'b.dll').exists())

    def test_success_clears_an_earlier_error(self):
        update = self.port / 'update'
        update.mkdir()
        (update / 'failed').write_text('earlier failure')
        self.assertTrue(sotc_update.apply_update(self.new, self.port, restart=False))
        self.assertIsNone(sotc_update.cleanup(self.port))


class WaitTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'requires real Windows process handles')
    def test_real_process_must_exit_before_the_wait_succeeds(self):
        with tempfile.TemporaryDirectory() as tmp:
            release = Path(tmp) / 'exit'
            child = sotc_update.subprocess.Popen([
                sys.executable, '-c',
                'import pathlib, sys, time\np = pathlib.Path(sys.argv[1])\n'
                'while not p.exists(): time.sleep(0.01)\n', str(release)])
            try:
                with self.assertRaises(TimeoutError):
                    sotc_update._wait_for_exit(child.pid, seconds=0)
                release.touch()
                sotc_update._wait_for_exit(child.pid, seconds=10)
                self.assertEqual(child.wait(timeout=5), 0)
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)

    def test_timeout_closes_the_handle_and_reports_failure(self):
        kernel = Mock()
        kernel.OpenProcess.return_value = 0x123456789
        kernel.WaitForSingleObject.return_value = 258  # WAIT_TIMEOUT
        with patch.object(sotc_update.os, 'name', 'nt'), \
                patch.object(sotc_update.ctypes, 'WinDLL', return_value=kernel, create=True):
            with self.assertRaises(TimeoutError):
                sotc_update._wait_for_exit(123, seconds=1)
        kernel.WaitForSingleObject.assert_called_once_with(0x123456789, 1000)
        kernel.CloseHandle.assert_called_once_with(0x123456789)
        self.assertIs(kernel.OpenProcess.restype, sotc_update.ctypes.c_void_p)

    def test_success_closes_the_handle(self):
        kernel = Mock()
        kernel.OpenProcess.return_value = 0x123456789
        kernel.WaitForSingleObject.return_value = 0
        with patch.object(sotc_update.os, 'name', 'nt'), \
                patch.object(sotc_update.ctypes, 'WinDLL', return_value=kernel, create=True):
            sotc_update._wait_for_exit(123)
        kernel.CloseHandle.assert_called_once_with(0x123456789)

    def test_open_failure_is_only_ignored_for_an_exited_process(self):
        kernel = Mock()
        kernel.OpenProcess.return_value = None
        with patch.object(sotc_update.os, 'name', 'nt'), \
                patch.object(sotc_update.ctypes, 'WinDLL', return_value=kernel, create=True), \
                patch.object(sotc_update.ctypes, 'get_last_error', return_value=87, create=True) as error:
            sotc_update._wait_for_exit(123)
            error.return_value = 5  # ERROR_ACCESS_DENIED does not prove the process has exited.
            with self.assertRaises(OSError):
                sotc_update._wait_for_exit(123)
        kernel.WaitForSingleObject.assert_not_called()


if __name__ == '__main__':
    unittest.main()
