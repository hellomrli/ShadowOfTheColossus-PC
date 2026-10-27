"""Every mode's original instructions are checked before any output is written."""
from paths import ROOT
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from game_fixture import executable
from prepare import parse_self
import sotc_modes


class ModePatchTests(unittest.TestCase):
    def fixture(self, changed=None, flags=5):
        sites = [(1, flags, address - sotc_modes.BASE,
                  b'\x90' * len(original) if index == changed else original)
                 for index, (address, original, _) in enumerate(sotc_modes.MODES['fps60game'])]
        return executable(extra=sites)

    def run_modes(self, data, out, modes='fps60game'):
        (out / 'eboot.bin').write_bytes(data)
        args = ['sotc_modes.py', str(out), '--out', str(out), '--modes', modes]
        with patch('sys.argv', args), contextlib.redirect_stdout(io.StringIO()):
            sotc_modes.main()

    def test_all_matching_sites_produce_the_expected_patch_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            self.run_modes(self.fixture(), out)
            blob = (out / 'patches.bin').read_bytes()
            expected = struct.pack('<8sQQ', b'BBPATCH2', sotc_modes.BASE, 3)
            for address, _, data in sotc_modes.MODES['fps60game']:
                expected += struct.pack('<QQ', address - sotc_modes.BASE, len(data)) + data
            self.assertEqual(blob, expected)
            self.assertTrue((out / 'cmdline.bin').is_file())

    def test_matching_layout_with_any_changed_mode_site_is_rejected(self):
        for index, (address, _, _) in enumerate(sotc_modes.MODES['fps60game']):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp)
                data = self.fixture(changed=index)
                elf, _, ph, _, _ = parse_self(data)
                self.assertTrue(sotc_modes.is_supported(elf, ph))
                with self.assertRaisesRegex(SystemExit, f'unexpected original bytes at {address:#x}'):
                    self.run_modes(data, out, modes='fps60,fps60game')
                self.assertFalse((out / 'patches.bin').exists())
                self.assertFalse((out / 'cmdline.bin').exists())

    def test_missing_mode_site_is_rejected_even_with_matching_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with self.assertRaisesRegex(SystemExit, 'unexpected original bytes'):
                self.run_modes(executable(), out)
            self.assertFalse((out / 'patches.bin').exists())

    def test_mode_sites_in_nonexecutable_segments_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            with self.assertRaisesRegex(SystemExit, 'unexpected original bytes'):
                self.run_modes(self.fixture(flags=4), out)

    def test_incompatible_layout_keeps_code_patches_and_cvars_disabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / 'cmdline.bin').write_bytes(b'stale command line')
            self.run_modes(executable(False), out, modes='fps60,fps60game,af16')
            self.assertEqual((out / 'patches.bin').read_bytes(),
                             struct.pack('<8sQQ', b'BBPATCH2', sotc_modes.BASE, 0))
            self.assertFalse((out / 'cmdline.bin').exists())


if __name__ == '__main__':
    unittest.main()
