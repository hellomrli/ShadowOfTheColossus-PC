from paths import ROOT
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
from game_fixture import executable, metadata
from prepare import parse_self, inspect_libc, nid, prepare, resource_inventory


def fixture():
    data = bytearray(0x210)
    data[:4] = b'O\x15=\x1d'
    struct.pack_into('<H', data, 24, 1)
    struct.pack_into('<QQQQ', data, 32, 0x800, 0x200, 16, 16)
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 64,
                     b'\x7fELF\x02\x01\x01\x09', 0xfe10, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 128, 1, 5, 0x1000, 0, 0, 16, 32, 0x1000)
    data[0x200:] = bytes(range(16))
    return data


class SelfTests(unittest.TestCase):
    def test_libc_ret_contract_is_verified_from_symbol_and_code(self):
        def libc(instruction):
            data=bytearray(0x320)
            data[:4]=b'O\x15=\x1d'
            struct.pack_into('<H',data,24,2)
            struct.pack_into('<QQQQ',data,32,0x800,0x200,16,16)
            struct.pack_into('<QQQQ',data,64,0x100800,0x220,256,256)
            struct.pack_into('<16sHHIQQQIHHHHHH',data,96,b'\x7fELF\x02\x01\x01\x09',0xfe18,62,1,0,64,0,0,64,56,3,0,0,0)
            struct.pack_into('<IIQQQQQQ',data,160,1,5,0x1000,0,0,16,16,0x1000)
            struct.pack_into('<IIQQQQQQ',data,216,0x61000000,4,0x2000,0,0,256,0,16)
            struct.pack_into('<IIQQQQQQ',data,272,2,4,0x20b0,0,0,80,80,8)
            data[0x200]=instruction
            name=b'bzQExy189ZI#C#A\0'
            data[0x220:0x220+len(name)]=name
            struct.pack_into('<IBBHQQ',data,0x260,0,0x12,0,1,0,1)
            for i,(tag,value) in enumerate([(0x61000035,0),(0x61000037,32),(0x61000039,64),(0x6100003f,24),(0,0)]):
                struct.pack_into('<QQ',data,0x2d0+16*i,tag,value)
            return data
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'libc.prx'
            path.write_bytes(libc(0xc3))
            proof=inspect_libc(path)
            self.assertTrue(proof['init_env_is_ret'])
            self.assertEqual(proof['bytes'],'c3')
            path.write_bytes(libc(0x90))
            self.assertFalse(inspect_libc(path)['init_env_is_ret'])
        self.assertEqual(nid('_init_env'),'bzQExy189ZI')

    def test_file_offset_is_not_virtual_address(self):
        elf, header, ph, segments, missing = parse_self(fixture())
        self.assertEqual(elf[0x1000:0x1010], bytes(range(16)))
        self.assertEqual(ph[0]['vaddr'], 0)
        self.assertEqual(missing, [])

    def test_truncated_payload_rejected(self):
        with self.assertRaisesRegex(ValueError, 'out of bounds'):
            parse_self(fixture()[:-1])

    def test_encrypted_or_compressed_payload_rejected(self):
        for flag in (2, 8):
            data = fixture()
            struct.pack_into('<Q', data, 32, 0x800 | flag)
            with self.assertRaisesRegex(ValueError, 'still encrypted'):
                parse_self(data)

    def test_missing_required_segment_rejected(self):
        data = fixture()
        struct.pack_into('<Q', data, 32, 0)
        with self.assertRaisesRegex(ValueError, 'required segment'):
            parse_self(data)

    def test_bad_program_header_index_rejected(self):
        data = fixture()
        struct.pack_into('<Q', data, 32, 0x100800)
        with self.assertRaisesRegex(ValueError, 'segment index'):
            parse_self(data)


class ResourceFixture(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.base, self.update = (Path(tmp.name) / name for name in ('base', 'update'))

    def asset(self, game, name, data):
        path = game / 'dvdroot_ps4' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


class ResourceInventoryTests(ResourceFixture):
    def test_missing_resource_roots_are_reported_explicitly(self):
        report = resource_inventory(self.base, self.update)
        self.assertEqual(report['resources'], {})
        self.assertEqual(report['resource_bytes'], 0)
        self.assertEqual([root['exists'] for root in report['resource_roots']], [False, False])

    def test_update_replaces_base_files_instead_of_double_counting(self):
        self.asset(self.base, 'data/common.bin', b'base')
        self.asset(self.base, 'textures/base.bin', b'base texture')
        self.asset(self.update, 'data/common.bin', b'updated')
        self.asset(self.update, 'data/new.bin', b'new')
        report = resource_inventory(self.base, self.update)
        self.assertEqual(report['resources'], {'data': 2, 'textures': 1})
        self.assertEqual(report['resource_bytes'], len(b'updatedbase texturenew'))
        self.assertEqual([root['role'] for root in report['resource_roots']], ['base', 'update'])

    def test_empty_update_file_still_overrides_a_nonempty_base_file(self):
        self.asset(self.base, 'data/common.bin', b'base')
        self.asset(self.update, 'data/common.bin', b'')
        report = resource_inventory(self.base, self.update)
        self.assertEqual(report['resources'], {'data': 1})
        self.assertEqual(report['resource_bytes'], 0)

    def test_same_base_and_update_path_is_scanned_only_once(self):
        self.asset(self.base, 'data/common.bin', b'base')
        report = resource_inventory(self.base, self.base)
        self.assertEqual(len(report['resource_roots']), 1)
        self.assertEqual(report['resource_bytes'], 4)


class AnalysisReportTests(ResourceFixture):
    def game(self, folder, supported=True):
        tags = [(0x61000035, 0), (0x61000037, 1), (0x61000039, 8), (0x6100003f, 24),
                (0x61000029, 32), (0x6100002d, 0), (0x6100002f, 32), (0x61000031, 0), (0, 0)]
        dynamic = b''.join(struct.pack('<QQ', *tag) for tag in tags)
        data = executable(supported, extra=[(1, 5, 0, b'\xc3'),
                                           (0x61000000, 4, 0, bytes(32)), (2, 4, 0, dynamic)])
        (folder / 'sce_sys').mkdir(parents=True)
        (folder / 'sce_module').mkdir()
        (folder / 'eboot.bin').write_bytes(data)
        (folder / 'sce_module/libc.prx').write_bytes(b'fixture libc, inspected through a mock')
        (folder / 'sce_sys/param.sfo').write_bytes(
            metadata(content_id='HP9000-CUSA08809_00-SOTC0000000000AS'))

    def report(self, source, **kwargs):
        out = source / 'prepared'
        with patch('prepare.inspect_libc', return_value={'init_env_is_ret': True}), \
                contextlib.redirect_stdout(io.StringIO()):
            prepare(source, out, **kwargs)
        return json.loads((out / 'analysis.json').read_text(encoding='utf-8'))

    def test_asian_metadata_does_not_override_matching_code_evidence(self):
        self.game(self.base)
        report = self.report(self.base)
        self.assertEqual(report['content_region'], 'Asia')
        self.assertTrue(report['compatibility']['signature_matches'])
        self.assertEqual(report['source_sha256'], hashlib.sha256((self.base / 'eboot.bin').read_bytes()).hexdigest())
        self.assertTrue(any('publisher prefix indicates Asia' in warning for warning in report['warnings']))
        self.assertTrue(any('No game resources found' in warning for warning in report['warnings']))
        self.assertEqual(report['validation'], {'execution': 'not_run', 'vulkan': 'not_run'})
        self.assertEqual(report['status'], 'Prepared only; execution and Vulkan are tested separately.')

    def test_mismatching_code_is_reported_even_with_the_expected_title_and_version(self):
        self.game(self.base, supported=False)
        report = self.report(self.base)
        self.assertEqual(report['sfo']['TITLE_ID'], 'CUSA08809')
        self.assertEqual(report['sfo']['APP_VER'], '01.01')
        self.assertFalse(report['compatibility']['signature_matches'])
        self.assertTrue(any('patches must remain disabled' in warning for warning in report['warnings']))

    def test_merged_image_uses_actual_base_and_update_resource_paths(self):
        source = self.base.parent / 'merged-image'
        self.game(source)
        self.asset(self.base, 'data/common.bin', b'base')
        self.asset(self.update, 'data/common.bin', b'updated')
        report = self.report(source, app0=self.base, update=self.update)
        self.assertEqual(report['resources'], {'data': 1})
        self.assertEqual(report['resource_bytes'], 7)
        self.assertFalse(any('No game resources found' in warning for warning in report['warnings']))
        self.assertEqual(report['resource_roots'][0]['path'], str((self.base / 'dvdroot_ps4').resolve()))

    def test_empty_resource_files_get_a_distinct_copy_warning(self):
        self.game(self.base)
        self.asset(self.base, 'data/empty.bin', b'')
        report = self.report(self.base)
        self.assertEqual(report['resources'], {'data': 1})
        self.assertTrue(any('All game resource files are empty' in warning for warning in report['warnings']))


class StandaloneScriptTests(unittest.TestCase):
    def test_preparation_scripts_import_from_a_staged_copy_without_the_checkout(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / 'scripts'
            stage.mkdir()
            for name in ('prepare.py', 'sotc_compatibility.py', 'sotc_cpu.py', 'sotc_modes.py'):
                shutil.copy2(ROOT / 'scripts' / name, stage / name)
            env = {key: value for key, value in os.environ.items() if key != 'PYTHONPATH'}
            for name in ('prepare.py', 'sotc_cpu.py', 'sotc_modes.py'):
                with self.subTest(script=name):
                    result = subprocess.run([sys.executable, '-S', str(stage / name), '--help'],
                                            cwd=tmp, env=env, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('--out', result.stdout)


if __name__ == '__main__':
    unittest.main()
