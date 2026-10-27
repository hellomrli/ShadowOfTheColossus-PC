#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared executable-byte evidence for the existing EU 1.01 patch layout.

Matching these signatures is not a whole-executable hash check. Individual code
patches must also check their own original bytes before writing.
"""

BASE = 0x400000
# The LEA that loads the text appended to the engine's command line.
CMDLINE_LEA = 0x009367b9
CMDLINE_ORIGINAL = '+VSyncWindow=10%'
VERSION_CHECKS = [
    (CMDLINE_LEA, bytes.fromhex('488d352debad00')),
    (0x01390090, bytes.fromhex('ff25a20567')),
]


def image_bytes(elf, ph, vaddr, length):
    for p in ph:
        if (p['type'] in (1, 0x61000010) and p['vaddr'] <= vaddr
                and vaddr + length <= p['vaddr'] + p['filesz']):
            offset = p['offset'] + vaddr - p['vaddr']
            return bytes(elf[offset:offset + length])
    return None


def code_bytes(elf, ph, vaddr, length):
    executable = [p for p in ph if p['type'] == 1 and p['flags'] & 1]
    return image_bytes(elf, executable, vaddr, length)


def compatibility_report(elf, ph):
    checks = []
    for address, original in VERSION_CHECKS:
        actual = code_bytes(elf, ph, address - BASE, len(original))
        checks.append(dict(address=hex(address), vaddr=hex(address - BASE),
                           expected=original.hex(), actual=actual.hex() if actual is not None else None,
                           matches=actual == original))
    return dict(patch_layout='EU 1.01', signature_matches=all(c['matches'] for c in checks),
                verification='original bytes in executable segments, not metadata; not a full-file hash check',
                checks=checks)


def is_supported(elf, ph):
    return all(code_bytes(elf, ph, address - BASE, len(original)) == original
               for address, original in VERSION_CHECKS)


def content_region(content_id):
    """Informational publisher prefix only; never authorizes an executable patch."""
    return {'EP': 'Europe', 'UP': 'North America', 'HP': 'Asia', 'JP': 'Japan'}.get(
        content_id[:2] if isinstance(content_id, str) else '', 'unknown')
