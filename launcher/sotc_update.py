# SPDX-License-Identifier: GPL-2.0-or-later
"""sotc: launcher self-update from the port's GitHub releases.

The launcher asks GitHub for the latest release (SOTC_UPDATE_URL replaces the API address, for
tests: a file:// or http:// URL of the same JSON). When its tag is newer than the running
version and the user agrees, SotC-PC.zip is downloaded to <port>/update/, checked (size, the
SHA-256 digest GitHub reports for the asset, zip integrity, layout) and unpacked to
<port>/update/new/SotC-PC/.

The files are replaced by a copy of the running launcher (SotC.exe and _internal/ copied to
<port>/update/applier/), started with --apply-update once the launcher has closed, so no file in
use is overwritten. It copies the new files over the port folder, never touching user/ (saves,
shader caches), update/ or an existing sotc.ini, and leaves files the release no longer has in
place. Every replaced file is backed up first; on any error the backups are put back. An
incomplete rollback keeps update/backup and blocks further updates until manual recovery.
The launcher is restarted only after a successful update or rollback. update/update.log
records what happened.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
import urllib.request
import zipfile

# Stay on this fork's stable channel; GitHub's /latest endpoint excludes prereleases.
RELEASES_URL = 'https://api.github.com/repos/hellomrli/ShadowOfTheColossus-PC/releases/latest'
ASSET_NAME = 'SotC-PC.zip'
TOP = 'SotC-PC'
KEEP = ('user', 'update')  # top-level names an update never writes into
USER_AGENT = 'SotC-PC-launcher'


def version_key(text):
    """'0.57-opt5' -> (0, 57, 5); '0.58' -> (0, 58)."""
    return tuple(int(n) for n in re.findall(r'\d+', text or ''))


def _open(url, timeout):
    request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json',
                                                   'User-Agent': USER_AGENT})
    return urllib.request.urlopen(request, timeout=timeout)


def latest_release(timeout=10):
    """The latest release with a SotC-PC.zip asset, as a dict, or None."""
    with _open(os.environ.get('SOTC_UPDATE_URL') or RELEASES_URL, timeout) as response:
        data = json.load(response)
    asset = next((a for a in data.get('assets', []) if a.get('name') == ASSET_NAME), None)
    if not asset or not data.get('tag_name'):
        return None
    digest = asset.get('digest') or ''
    return {'version': data['tag_name'].lstrip('vV'), 'name': data.get('name') or data['tag_name'],
            'notes': data.get('body') or '', 'page': data.get('html_url') or '',
            'url': asset['browser_download_url'], 'size': int(asset.get('size') or 0),
            'sha256': digest[7:].lower() if digest.startswith('sha256:') else ''}


def is_newer(release, current):
    return bool(release) and version_key(release['version']) > version_key(current)


def _release_parts(name):
    # Validate Windows names even on Linux: extraction must not normalize a path into user/,
    # an alternate data stream, a device name, or another archive member.
    parts = name.removesuffix('/').split('/')
    invalid = any(not p or p in ('.', '..') or p.endswith((' ', '.')) or
                  re.search(r'[\\:<>"|?*\x00-\x1f]', p) or
                  re.match(r'(?i)^(con|prn|aux|nul|com[1-9\u00b9\u00b2\u00b3]|lpt[1-9\u00b9\u00b2\u00b3])(?:[ .]|$)', p)
                  for p in parts)
    if invalid or parts[0] != TOP:
        raise ValueError(f'unexpected path in the release: {name}')
    return parts


def _check_no_links(root, rel):
    path = root
    for part in rel.parts:
        path /= part
        if path.is_symlink() or (hasattr(path, 'is_junction') and path.is_junction()):
            raise ValueError(f'update path is a link or junction: {path}')


def _check_recovery(update_dir):
    if (update_dir / 'recovery-needed').exists():
        raise RuntimeError(f'previous update needs recovery; restore {update_dir / "backup"} '
                           'before updating again')


def download(release, update_dir, progress=None):
    """Downloads and checks the release zip, unpacks it; returns the new SotC-PC folder."""
    update_dir = Path(update_dir)
    _check_no_links(update_dir.parent, Path(update_dir.name))
    _check_recovery(update_dir)
    update_dir.mkdir(parents=True, exist_ok=True)
    part = update_dir / (ASSET_NAME + '.part')
    _check_no_links(update_dir, Path(part.name))
    digest, done = hashlib.sha256(), 0
    with _open(release['url'], 60) as response, open(part, 'wb') as out:
        total = int(response.headers.get('Content-Length') or release['size'] or 0)
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            if progress:
                progress(done, total)
    if release['size'] and done != release['size']:
        raise ValueError(f'download incomplete: {done} of {release["size"]} bytes')
    if release['sha256'] and digest.hexdigest() != release['sha256']:
        raise ValueError('download damaged: SHA-256 does not match the release')
    new = update_dir / 'new'
    _check_no_links(update_dir, Path('new'))
    if new.exists():
        shutil.rmtree(new)
    with zipfile.ZipFile(part) as archive:
        names = archive.namelist()
        if f'{TOP}/SotC.exe' not in names:
            raise ValueError(f'not a SotC-PC release (no {TOP}/SotC.exe)')
        seen = set()
        for info in archive.infolist():
            parts = _release_parts(info.filename)
            key = '/'.join(parts).casefold()
            if key in seen:
                raise ValueError(f'duplicate path in the release: {info.filename}')
            seen.add(key)
            if stat.S_ISLNK(info.external_attr >> 16):
                raise ValueError(f'unexpected path in the release (symlink): {info.filename}')
        if archive.testzip() is not None:
            raise ValueError('the release zip is damaged')
        archive.extractall(new)
    part.unlink()
    return new / TOP


def start_applier(new_root, port_dir):
    """Copies the running launcher to update/applier and starts it with --apply-update; the
    caller then closes. Only for the frozen launcher (SotC.exe + _internal/)."""
    _check_no_links(port_dir, Path('update/applier'))
    _check_recovery(port_dir / 'update')
    exe = Path(sys.executable).resolve()
    applier = port_dir / 'update' / 'applier'
    if applier.exists():
        shutil.rmtree(applier)
    applier.mkdir(parents=True)
    shutil.copy2(exe, applier / exe.name)
    internal = exe.parent / '_internal'
    if internal.is_dir():
        shutil.copytree(internal, applier / '_internal')
    flags = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0)
    subprocess.Popen([str(applier / exe.name), '--apply-update', str(new_root), str(port_dir),
                      str(os.getpid())], cwd=str(applier), creationflags=flags, close_fds=True)


def _wait_for_exit(pid, seconds=60):
    if not pid or os.name != 'nt':
        return
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    SYNCHRONIZE = 0x00100000
    handle = kernel.OpenProcess(SYNCHRONIZE, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # ERROR_INVALID_PARAMETER: the process has already exited.
            return
        raise OSError(error, f'cannot wait for launcher process {pid}')
    try:
        result = kernel.WaitForSingleObject(handle, seconds * 1000)
        if result == 258:  # WAIT_TIMEOUT
            raise TimeoutError(f'launcher process {pid} is still running')
        if result != 0:  # WAIT_OBJECT_0
            raise OSError(ctypes.get_last_error(), f'waiting for launcher process {pid} failed')
    finally:
        kernel.CloseHandle(handle)


def _copy(src, dst):
    # A file may stay locked for a moment (antivirus scan of the new exe): retry for ~10 s.
    for attempt in range(20):
        try:
            shutil.copy2(src, dst)
            return
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.5)


def apply_update(new_root, port_dir, pid=0, restart=True):
    """Copies new_root over port_dir (see the module docstring). Returns True on success."""
    new_root, port_dir = Path(new_root), Path(port_dir)
    update_dir = port_dir / 'update'
    _check_no_links(port_dir, Path('update'))
    update_dir.mkdir(parents=True, exist_ok=True)
    for name in ('update.log', 'backup', 'failed', 'recovery-needed'):
        _check_no_links(update_dir, Path(name))
    log = open(update_dir / 'update.log', 'a', encoding='utf-8')

    def note(text):
        try:
            log.write(time.strftime('%Y-%m-%d %H:%M:%S ') + text + '\n')
            log.flush()
        except OSError:
            pass  # A full disk must not stop the rollback.

    backup = update_dir / 'backup'
    recovery = update_dir / 'recovery-needed'
    done = []  # (installed file, its backup or None)
    ok = started = exited = False
    try:
        _check_recovery(update_dir)
        _wait_for_exit(pid)
        exited = True
        if not (new_root / 'SotC.exe').is_file():
            raise ValueError(f'{new_root} is not a SotC-PC folder')
        has_ini = any(p.name.casefold() == 'sotc.ini' for p in port_dir.iterdir())
        files = []
        for src in sorted(new_root.rglob('*')):
            rel = src.relative_to(new_root)
            _release_parts(f'{TOP}/{rel.as_posix()}')
            if rel.parts[0].casefold() in KEEP or (rel.as_posix().casefold() == 'sotc.ini' and has_ini):
                continue
            _check_no_links(new_root, rel)
            _check_no_links(port_dir, rel)
            if src.is_file():
                files.append((src, rel))
        if backup.exists():
            shutil.rmtree(backup)
        # Leave this marker behind on interruption or failed rollback, before changing a file.
        recovery.write_text(f'Restore original files from {backup}; see update.log.\n', encoding='utf-8')
        started = True
        for src, rel in files:
            dst = port_dir / rel
            saved = None
            if dst.exists():
                saved = backup / rel
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(dst, saved)
            done.append((dst, saved))
            dst.parent.mkdir(parents=True, exist_ok=True)
            _copy(src, dst)
        note(f'updated: {len(done)} files from {new_root}')
        (update_dir / 'failed').unlink(missing_ok=True)
        recovery.unlink()
        ok = True
    except Exception as error:  # put every replaced file back
        note(f'FAILED: {error!r}; restoring {len(done)} files')
        restored = True
        for dst, saved in reversed(done):
            try:
                if saved:
                    _copy(saved, dst)
                elif dst.exists():
                    dst.unlink()
            except OSError as restore_error:
                restored = False
                note(f'could not restore {dst}: {restore_error!r}')
        if restored and started:
            recovery.unlink(missing_ok=True)
        message = str(error)
        if recovery.exists():
            message += f'\nBackup kept at {backup}; recover it before removing {recovery}.'
        (update_dir / 'failed').write_text(message, encoding='utf-8')
    finally:
        log.close()
    if restart and exited and not recovery.exists() and (port_dir / 'SotC.exe').is_file():
        flags = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0)
        subprocess.Popen([str(port_dir / 'SotC.exe')], cwd=str(port_dir), creationflags=flags, close_fds=True)
    return ok


def cleanup(port_dir):
    """Remove staging files; keep backups and warnings until an incomplete rollback is recovered."""
    update_dir = Path(port_dir) / 'update'
    try:
        _check_no_links(Path(port_dir), Path('update'))
    except (OSError, ValueError) as error:
        return str(error)
    failed = update_dir / 'failed'
    error = failed.read_text(encoding='utf-8', errors='replace') if failed.is_file() else None
    if (update_dir / 'recovery-needed').exists():
        # Another launcher may open while the applier is still running. Do not remove its
        # inputs or backups; after interruption they are also useful for manual recovery.
        return error or (f'Previous update did not complete. Backup kept at {update_dir / "backup"}; '
                         'recover it before removing update/recovery-needed.')
    for name in ('new', 'applier', 'backup', 'failed', ASSET_NAME + '.part'):
        path = update_dir / name
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)  # the applier may still be closing
        elif path.exists():
            try:
                path.unlink()
            except OSError:
                pass
    return error
