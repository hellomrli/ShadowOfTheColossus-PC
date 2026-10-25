from paths import ROOT
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from game_fixture import executable, metadata

sys.path.insert(0, str(ROOT / 'launcher'))
import sotc_launcher_win as launcher


class CompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.port = Path(self.tmp.name)
        self.game = self.port / 'CUSA08804'
        (self.game / 'sce_sys').mkdir(parents=True)
        (self.game / 'sce_sys/param.sfo').write_bytes(metadata())
        (self.game / 'eboot.bin').write_bytes(executable(False))
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, {}, clear=True).start()
        patch.object(launcher, 'PORT_DIR', self.port).start()

    def test_metadata_version_is_not_executable_verification(self):
        self.assertEqual(launcher.game_info(self.game), ('CUSA08809', '01.01'))
        self.assertFalse(launcher.game_supports_modes(self.game))

    def test_folder_name_is_not_used_to_identify_an_executable(self):
        (self.game / 'eboot.bin').write_bytes(executable())
        self.assertTrue(launcher.game_supports_modes(self.game))

    def test_adjacent_update_is_the_executable_that_is_checked(self):
        for suffix in ('-UPDATE', '-patch'):
            with self.subTest(suffix=suffix):
                update = self.game.with_name(self.game.name + suffix)
                update.mkdir()
                (update / 'eboot.bin').write_bytes(executable())
                self.assertTrue(launcher.game_supports_modes(self.game))
                (update / 'eboot.bin').unlink()
                self.assertFalse(launcher.game_supports_modes(self.game))
                update.rmdir()

    def test_explicit_update_takes_precedence_over_adjacent_update(self):
        update = self.game.with_name(self.game.name + '-UPDATE')
        update.mkdir()
        (update / 'eboot.bin').write_bytes(executable())
        os.environ['SOTC_UPDATE_DIR'] = str(self.port / 'missing')
        self.assertFalse(launcher.game_supports_modes(self.game))
        self.assertTrue(launcher.game_supports_modes(self.game, f'SOTC_UPDATE_DIR={update}'))

    def test_ini_relative_update_is_resolved_from_the_port_directory(self):
        update = self.port / 'updated'
        update.mkdir()
        (update / 'eboot.bin').write_bytes(executable())
        (self.port / 'sotc.ini').write_text('SOTC_UPDATE_DIR=updated\n', encoding='utf-8')
        self.assertTrue(launcher.game_supports_modes(self.game))
        os.environ['SOTC_UPDATE_DIR'] = str(self.port / 'missing')
        self.assertFalse(launcher.game_supports_modes(self.game))

    def test_update_without_executable_keeps_the_base_executable(self):
        (self.game / 'eboot.bin').write_bytes(executable())
        update = self.game.with_name(self.game.name + '-UPDATE')
        update.mkdir()
        self.assertTrue(launcher.game_supports_modes(self.game))
        os.environ['SOTC_UPDATE_DIR'] = str(self.port / 'missing')
        self.assertTrue(launcher.game_supports_modes(self.game))
        (update / 'eboot.bin').write_bytes(b'')
        self.assertFalse(launcher.game_supports_modes(self.game, f'SOTC_UPDATE_DIR={update}'))

    def test_extra_game_directory_overrides_the_launcher_selection(self):
        other = self.port / 'other'
        other.mkdir()
        (other / 'eboot.bin').write_bytes(executable())
        self.assertTrue(launcher.game_supports_modes(self.game, 'SOTC_GAME_DIR=other'))


if __name__ == '__main__':
    unittest.main()
