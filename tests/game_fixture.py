"""Small synthetic game metadata/executable; no game or firmware bytes are needed."""
import struct
from sotc_modes import BASE, VERSION_CHECKS


def executable(supported=True, eh=True, extra=()):
    segments = [(1, 5, address - BASE, original) for address, original in VERSION_CHECKS]
    if not supported:
        kind, flags, address, code = segments[0]
        segments[0] = (kind, flags, address, b'\x90' * len(code))
    if eh:
        segments.extend([(1, 4, 0x1500000, b'\0' * 32), (0x6474e550, 4, 0x1500000, b'\0' * 32)])
    segments.extend(extra)
    data = bytearray(64 + 56 * len(segments))
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 0, b'\x7fELF\x02\x01\x01\x09',
                     0xfe10, 62, 1, 0, 64, 0, 0, 64, 56, len(segments), 0, 0, 0)
    for i, (kind, flags, address, code) in enumerate(segments):
        struct.pack_into('<IIQQQQQQ', data, 64 + i * 56,
                         kind, flags, len(data), address, 0, len(code), len(code), 1)
        data.extend(code)
    return data


def metadata(serial='CUSA08809', version='01.01', content_id=None):
    values = [('TITLE_ID', serial), ('APP_VER', version)]
    if content_id is not None:
        values.append(('CONTENT_ID', content_id))
    keys = b''
    payload = b''
    entries = b''
    for key, value in values:
        encoded = value.encode('ascii') + b'\0'
        entries += struct.pack('<HHIII', len(keys), 0x204, len(encoded), len(encoded), len(payload))
        keys += key.encode('ascii') + b'\0'
        payload += encoded
    key_offset = 20 + len(entries)
    return struct.pack('<4sIIII', b'\0PSF', 0x101, key_offset, key_offset + len(keys), len(values)) + entries + keys + payload
