from paths import ROOT
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from game_fixture import executable, metadata
import sotc_cpu


class CpuPatchTests(unittest.TestCase):
    def prepare(self, supported=True, serial='CUSA08809', eh=True):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            (game / 'sce_sys').mkdir()
            (game / 'eboot.bin').write_bytes(executable(supported, eh))
            (game / 'sce_sys/param.sfo').write_bytes(metadata(serial))
            output = io.StringIO()
            with patch('sys.argv', ['sotc_cpu.py', str(game), '--out', str(game)]), contextlib.redirect_stdout(output):
                sotc_cpu.main()
            blob = (game / 'sotc-cpu.bin').read_bytes()
            return struct.unpack('<8sQQQ', blob[:32]), blob[32:], output.getvalue()

    def test_verified_eu_executable_gets_the_known_patches(self):
        header, functions, log = self.prepare()
        self.assertEqual(header, (b'SOTCCPU1', 0x1500000, 32, 3))
        self.assertEqual(struct.unpack('<QQQ', functions), sotc_cpu.RED_ZONE_FUNCTIONS['CUSA08809'])
        self.assertIn('verified=True', log)

    def test_sfo_cannot_authorize_offsets_for_another_executable(self):
        header, functions, log = self.prepare(supported=False)
        self.assertEqual(header, (b'SOTCCPU1', 0x1500000, 32, 0))
        self.assertEqual(functions, b'')
        self.assertIn('fixed-address red-zone patches disabled', log)

    def test_unknown_serial_never_gets_fixed_offsets(self):
        header, functions, _ = self.prepare(serial='CUSA08804')
        self.assertEqual(header[3], 0)
        self.assertEqual(functions, b'')

    def test_absent_eh_header_is_reported_without_crashing(self):
        header, _, log = self.prepare(supported=False, eh=False)
        self.assertEqual(header, (b'SOTCCPU1', 0, 0, 0))
        self.assertIn('eh_frame_hdr=absent', log)


if __name__ == '__main__':
    unittest.main()
