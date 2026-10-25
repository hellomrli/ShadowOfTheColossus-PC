"""Copy the native PE dependency closure from CLANG64, without executing the binaries."""
import argparse
from collections import deque
import os
from pathlib import Path
import re
import shutil
import subprocess


def imports(binary, objdump):
    output = subprocess.check_output([str(objdump), '-p', str(binary)], text=True,
                                     encoding='utf-8', errors='replace')
    return re.findall(r'^\s*DLL Name:\s*(\S+)\s*$', output, re.MULTILINE)


def dll_index(directory):
    return {path.name.casefold(): path for path in directory.iterdir()
            if path.is_file() and path.suffix.casefold() == '.dll'}


def collect(destination, library_dir, system_dir, objdump='llvm-objdump'):
    destination, library_dir, system_dir = map(Path, (destination, library_dir, system_dir))
    bundled = dll_index(destination)
    available = dll_index(library_dir)
    system = dll_index(system_dir)
    pending = deque(path for path in destination.iterdir()
                    if path.is_file() and path.suffix.casefold() in ('.exe', '.dll'))
    scanned, copied = set(), []
    while pending:
        binary = pending.popleft()
        key = binary.name.casefold()
        if key in scanned:
            continue
        scanned.add(key)
        for name in imports(binary, objdump):
            if any(char in name for char in '/\\:') or name in ('.', '..'):
                raise ValueError(f'{binary.name} imports an invalid DLL name: {name}')
            key = name.casefold()
            if key in bundled:
                pending.append(bundled[key])
            elif key in available:
                source = available[key]
                target = destination / source.name
                shutil.copy2(source, target)
                bundled[key] = target
                copied.append(target)
                pending.append(target)
            elif key not in system and not key.startswith(('api-ms-', 'ext-ms-')):
                raise FileNotFoundError(f'{binary.name} requires {name}, missing from {library_dir}')
    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('library_dir', type=Path)
    parser.add_argument('--system-dir', type=Path,
                        default=Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32')
    parser.add_argument('--objdump', default='llvm-objdump')
    args = parser.parse_args()
    copied = collect(args.destination, args.library_dir, args.system_dir, args.objdump)
    print(f'Native dependency closure: copied {len(copied)} DLLs from {args.library_dir}')


if __name__ == '__main__':
    main()
