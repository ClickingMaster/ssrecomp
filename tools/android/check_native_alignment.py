"""Check that every packaged 64-bit native library supports 16 KB ELF load alignment."""
from pathlib import Path
import struct
import sys
import zipfile


def check(data, name):
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(name + ': expected a little-endian ELF64 library')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    entsize, count = struct.unpack_from('<HH', data, 54)
    if entsize < 56 or phoff + count * entsize > len(data):
        raise ValueError(name + ': invalid program header table')
    loads = 0
    for index in range(count):
        kind, flags, offset, address, physical, size, memory, alignment = struct.unpack_from('<IIQQQQQQ', data, phoff + index * entsize)
        if kind != 1:
            continue
        loads += 1
        if alignment < 16384 or (address - offset) % 16384:
            raise ValueError(name + ': PT_LOAD is not 16 KB aligned')
    if not loads:
        raise ValueError(name + ': no loadable segments')


def main(path):
    with zipfile.ZipFile(path) as archive:
        names = [n for n in archive.namelist() if n.startswith('lib/') and n.endswith('.so')]
        if not names:
            raise ValueError('No native libraries in APK')
        for name in names:
            check(archive.read(name), name)
            print('16 KB ELF alignment: OK', name)


if __name__ == '__main__':
    main(Path(sys.argv[1]))
