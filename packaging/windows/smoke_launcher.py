"""Run with SotC.exe --script after packaging; no game dump or GPU is required."""
import json
import os
from pathlib import Path
import sys
import tempfile
import tkinter

from PIL import Image
import sotc_update


def main():
    assert getattr(sys, 'frozen', False) and sys.platform == 'win32'
    port = Path(sys.executable).resolve().parent
    for name in ('SotC.exe', 'bin/bb-probe.exe', 'bin/bb-gpu-capabilities.exe',
                 'bin/sotc_dlss.dll', 'bin/nvngx_dlss.dll', 'bin/licenses-nvidia/LICENSE.txt',
                 'sotc.py', 'scripts/prepare.py', 'launcher/window_icon.ico',
                 'docs/sotc/CRASH-NVIDIA-2026-10-25.md', 'docs/sotc/DLSS.md',
                 'docs/sotc/STABILITY-CPU-2026-10-25.md',
                 'docs/sotc/UPSTREAM-REVIEW-2026-10-26.md', 'docs/sotc/UPSTREAM-BACKPORTS.md'):
        assert (port / name).is_file(), f'package missing {name}'
    assert sotc_update.RELEASES_URL == (
        'https://api.github.com/repos/hellomrli/ShadowOfTheColossus-PC/releases/latest')
    tcl_version = tkinter.Tcl().eval('info patchlevel')
    with Image.open(port / 'launcher' / 'window_icon.ico') as icon:
        icon.load()
        assert icon.width > 0 and icon.height > 0
    try:
        sotc_update._wait_for_exit(os.getpid(), seconds=0)
    except TimeoutError:
        pass
    else:
        raise AssertionError('the running launcher process must time out')
    with tempfile.TemporaryDirectory() as tmp:
        current, new = Path(tmp) / 'port', Path(tmp) / 'new'
        current.mkdir()
        new.mkdir()
        (current / 'SotC.exe').write_bytes(b'old')
        (current / 'sotc.ini').write_text('my settings', encoding='utf-8')
        (new / 'SotC.exe').write_bytes(b'new')
        (new / 'sotc.ini').write_text('defaults', encoding='utf-8')
        assert sotc_update.apply_update(new, current, restart=False)
        assert (current / 'SotC.exe').read_bytes() == b'new'
        assert (current / 'sotc.ini').read_text(encoding='utf-8') == 'my settings'
        assert sotc_update.cleanup(current) is None
    Path(sys.argv[1]).write_text(json.dumps({'status': 'ok', 'tcl': tcl_version}), encoding='utf-8')


if __name__ == '__main__':
    main()
