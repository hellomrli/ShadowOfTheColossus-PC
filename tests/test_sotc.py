"""Offline preparation cache and update overlays, without a game dump or native binary."""
from paths import ROOT
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sotc', ROOT / 'sotc.py')
sotc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sotc)


class PreparationCacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / 'game'
        self.update = self.root / 'patch'
        self.out = self.root / 'out'
        for folder in (self.game, self.update):
            (folder / 'sce_module').mkdir(parents=True)
            (folder / 'sce_sys').mkdir()
            (folder / 'eboot.bin').write_bytes(b'eboot')
            (folder / 'sce_sys' / 'param.sfo').write_bytes(b'metadata')
        (self.game / 'sce_module' / 'libc.prx').write_bytes(b'base libc')
        (self.update / 'sce_module' / 'libc.prx').write_bytes(b'updated libc')

    def test_metadata_changes_invalidate_the_prepared_image(self):
        before = sotc.fingerprint(self.game, [])
        (self.game / 'sce_sys' / 'param.sfo').write_bytes(b'new metadata and user parameters')
        self.assertNotEqual(sotc.fingerprint(self.game, []), before)

    def test_unchanged_inputs_keep_the_fingerprint(self):
        self.assertEqual(sotc.fingerprint(self.game, []), sotc.fingerprint(self.game, []))

    def test_update_overrides_base_without_recopying_on_every_launch(self):
        merged = sotc.merged_source(self.game, self.update, self.out)
        self.assertEqual((merged / 'sce_module' / 'libc.prx').read_bytes(), b'updated libc')
        with patch.object(sotc.shutil, 'copy2', wraps=shutil.copy2) as copy:
            sotc.merged_source(self.game, self.update, self.out)
        copy.assert_not_called()

    def test_removed_update_files_do_not_survive_in_the_overlay(self):
        extra = self.update / 'sce_module' / 'extra.prx'
        extra.write_bytes(b'old module')
        merged = sotc.merged_source(self.game, self.update, self.out)
        before = sotc.fingerprint(merged, [])
        extra.unlink()
        (self.update / 'sce_module' / 'libc.prx').unlink()
        sotc.merged_source(self.game, self.update, self.out)
        self.assertFalse((merged / 'sce_module' / 'extra.prx').exists())
        self.assertEqual((merged / 'sce_module' / 'libc.prx').read_bytes(), b'base libc')
        self.assertNotEqual(sotc.fingerprint(merged, []), before)

    def test_removed_metadata_does_not_survive_in_the_overlay(self):
        merged = sotc.merged_source(self.game, self.update, self.out)
        for folder in (self.game, self.update):
            (folder / 'sce_sys' / 'param.sfo').unlink()
        sotc.merged_source(self.game, self.update, self.out)
        self.assertFalse((merged / 'sce_sys' / 'param.sfo').exists())

    def test_switching_sources_with_identical_stats_refreshes_the_overlay(self):
        other = self.root / 'other-patch'
        shutil.copytree(self.update, other)
        eboot = other / 'eboot.bin'
        old_stat = eboot.stat()
        eboot.write_bytes(b'other')  # same size and timestamps, different dump
        os.utime(eboot, ns=(old_stat.st_atime_ns, old_stat.st_mtime_ns))
        merged = sotc.merged_source(self.game, self.update, self.out)
        before = sotc.fingerprint(merged, [])
        sotc.merged_source(self.game, other, self.out)
        self.assertEqual((merged / 'eboot.bin').read_bytes(), b'other')
        self.assertNotEqual(sotc.fingerprint(merged, []), before)

    def test_interrupted_overlay_refresh_is_retried(self):
        other = self.root / 'other-patch'
        shutil.copytree(self.update, other)
        eboot = other / 'eboot.bin'
        old_stat = eboot.stat()
        eboot.write_bytes(b'other')
        os.utime(eboot, ns=(old_stat.st_atime_ns, old_stat.st_mtime_ns))
        merged = sotc.merged_source(self.game, self.update, self.out)
        copy = shutil.copy2

        def interrupted_copy(src, dst):
            if src.name == 'libc.prx':
                raise OSError('copy interrupted')
            return copy(src, dst)

        with patch.object(sotc.shutil, 'copy2', side_effect=interrupted_copy):
            with self.assertRaises(OSError):
                sotc.merged_source(self.game, other, self.out)
        self.assertEqual((merged / 'eboot.bin').read_bytes(), b'other')
        sotc.merged_source(self.game, self.update, self.out)
        self.assertEqual((merged / 'eboot.bin').read_bytes(), b'eboot')

    def test_failed_preparation_invalidates_the_previous_success_stamp(self):
        self.out.mkdir()
        stamp = self.out / 'prepared.txt'
        stamp.write_text('previous successful inputs\n')
        for name in ('boot-linked.bin', 'content.bin', 'sotc-cpu.bin'):
            (self.out / name).write_bytes(b'old output')
        self.addCleanup(os.chdir, Path.cwd())
        with patch.object(sotc, 'PORT', self.root), \
                patch.dict(os.environ, {'SOTC_GAME_DIR': str(self.game)}, clear=True), \
                patch.object(sotc, 'run_script', side_effect=SystemExit(2)):
            with self.assertRaises(SystemExit):
                sotc.main()
        self.assertFalse(stamp.exists())


if __name__ == '__main__':
    unittest.main()
