"""Launcher and in-game upscaler choices share a persistent, explicit startup mode."""
from paths import ROOT
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(ROOT / 'launcher'))
import sotc_launcher_win as launcher  # noqa: E402


class UpscalerTests(unittest.TestCase):
    def env(self, mode, extra=''):
        with patch.object(launcher, 'game_info', return_value=None), \
                patch.object(launcher, 'window_icon_file', return_value=None):
            return launcher.game_environment({**launcher.APP_DEFAULTS, 'upscaler': mode,
                                              'extra_env': extra})

    def test_all_in_game_choices_exist_in_the_launcher(self):
        self.assertEqual({key for key, _ in launcher.UPSCALERS}, {'dlss', 'fsr1', 'bilinear'})

    def test_new_install_prefers_dlss_with_an_fsr_fallback(self):
        self.assertEqual(launcher.APP_DEFAULTS['upscaler'], 'dlss')

    def test_selection_is_separate_from_dlss_availability(self):
        for mode, fallback in (('dlss', '1'), ('fsr1', '1'), ('bilinear', '0')):
            with self.subTest(mode=mode), patch.dict(os.environ, {}, clear=True):
                env = self.env(mode)
                self.assertEqual(env.get('SOTC_UPSCALER'), mode)
                self.assertEqual(env['BB_FSR1'], fallback)
                # Keep NGX available so the in-game menu can enable DLSS without a restart.
                self.assertNotIn('SOTC_DLSS', env)

    def test_hard_disable_from_environment_is_kept(self):
        with patch.dict(os.environ, {'SOTC_DLSS': '0'}, clear=True):
            env = self.env('dlss')
        self.assertEqual(env['SOTC_DLSS'], '0')
        self.assertEqual(env['BB_FSR1'], '1')

    def test_extra_variables_can_override_the_selection(self):
        with patch.dict(os.environ, {}, clear=True):
            env = self.env('dlss', 'SOTC_UPSCALER=bilinear SOTC_DLSS=0 BB_FSR1=0')
        self.assertEqual((env['SOTC_UPSCALER'], env['SOTC_DLSS'], env['BB_FSR1']),
                         ('bilinear', '0', '0'))

    def test_saved_choices_survive_loading(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp) / 'settings.json'
            for mode in ('dlss', 'fsr1', 'bilinear'):
                with self.subTest(mode=mode):
                    config.write_text(json.dumps({'upscaler': mode}), encoding='utf-8')
                    with patch.object(launcher, 'CONFIG_FILE', config):
                        self.assertEqual(launcher.load_settings()['upscaler'], mode)


class WindowIconTests(unittest.TestCase):
    def test_clean_checkout_uses_the_shipped_ico(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow is needed for icon conversion')
        with tempfile.TemporaryDirectory() as tmp:
            port = Path(tmp)
            (port / 'launcher').mkdir()
            shutil.copy2(ROOT / 'launcher' / 'window_icon.ico', port / 'launcher' / 'window_icon.ico')
            with patch.object(launcher, 'PORT_DIR', port):
                icon = launcher.window_icon_file({**launcher.APP_DEFAULTS, 'user_dir': str(port / 'user')})
            self.assertIsNotNone(icon)
            with Image.open(icon) as image:
                self.assertEqual(image.size, (64, 64))
                self.assertEqual(image.format, 'BMP')


if __name__ == '__main__':
    unittest.main()
