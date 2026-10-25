"""Windows DLL closure tests use a fake import graph and need no native binaries."""
from paths import ROOT
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'collect_dlls', ROOT / 'packaging' / 'windows' / 'collect_dlls.py')
collect_dlls = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collect_dlls)


class DllClosureTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        self.dest, self.libs, self.system = (root / name for name in ('bin', 'clang64', 'system'))
        for folder in (self.dest, self.libs, self.system):
            folder.mkdir()
        (self.dest / 'game.exe').write_bytes(b'exe')

    def collect(self, graph):
        with patch.object(collect_dlls, 'imports',
                          side_effect=lambda binary, tool: graph.get(binary.name.casefold(), [])):
            return collect_dlls.collect(self.dest, self.libs, self.system)

    def test_transitive_dependencies_and_cycles(self):
        for name in ('direct.dll', 'indirect.dll', 'leaf.dll'):
            (self.libs / name).write_bytes(name.encode())
        copied = self.collect({'game.exe': ['direct.dll'], 'direct.dll': ['indirect.dll'],
                               'indirect.dll': ['direct.dll', 'leaf.dll']})
        self.assertEqual({path.name for path in copied}, {'direct.dll', 'indirect.dll', 'leaf.dll'})
        self.assertEqual((self.dest / 'leaf.dll').read_bytes(), b'leaf.dll')

    def test_import_names_are_case_insensitive(self):
        (self.libs / 'SDL3.DLL').write_bytes(b'sdl')
        copied = self.collect({'game.exe': ['sdl3.dll', 'SDL3.dll']})
        self.assertEqual(len(copied), 1)
        self.assertEqual(copied[0].name, 'SDL3.DLL')

    def test_existing_bundled_dll_is_kept_and_scanned(self):
        (self.dest / 'bridge.dll').write_bytes(b'custom')
        (self.libs / 'bridge.dll').write_bytes(b'other')
        (self.libs / 'child.dll').write_bytes(b'child')
        copied = self.collect({'game.exe': ['bridge.dll'], 'bridge.dll': ['child.dll']})
        self.assertEqual([path.name for path in copied], ['child.dll'])
        self.assertEqual((self.dest / 'bridge.dll').read_bytes(), b'custom')

    def test_system_dlls_and_api_sets_are_not_copied(self):
        (self.system / 'KERNEL32.dll').write_bytes(b'system')
        self.assertEqual(self.collect({'game.exe': [
            'kernel32.dll', 'api-ms-win-core-memory-l1-1-6.dll', 'ext-ms-win-test.dll']}), [])
        self.assertEqual([path.name for path in self.dest.iterdir()], ['game.exe'])

    def test_missing_dependency_stops_packaging(self):
        (self.libs / 'direct.dll').write_bytes(b'direct')
        with self.assertRaisesRegex(FileNotFoundError, 'direct.dll requires missing.dll'):
            self.collect({'game.exe': ['direct.dll'], 'direct.dll': ['missing.dll']})

    def test_imported_paths_are_rejected(self):
        for name in ('../outside.dll', '..\\outside.dll', 'C:outside.dll'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'invalid DLL name'):
                self.collect({'game.exe': [name]})

    def test_objdump_import_parsing(self):
        output = 'The Import Tables\n\tDLL Name: KERNEL32.dll\n\tDLL Name: libc++.dll\n'
        with patch.object(collect_dlls.subprocess, 'check_output', return_value=output) as dump:
            self.assertEqual(collect_dlls.imports(Path('game.exe'), 'objdump'),
                             ['KERNEL32.dll', 'libc++.dll'])
        self.assertEqual(dump.call_args.args[0], ['objdump', '-p', 'game.exe'])

    def test_objdump_failure_is_not_ignored(self):
        with patch.object(collect_dlls.subprocess, 'check_output',
                          side_effect=subprocess.CalledProcessError(1, 'objdump')):
            with self.assertRaises(subprocess.CalledProcessError):
                collect_dlls.collect(self.dest, self.libs, self.system)


if __name__ == '__main__':
    unittest.main()
