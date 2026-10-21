#!/usr/bin/env python3
"""Check the shipped DLL's PE32/x86, 5.1 header and CRT-free import contract.

This is a build artifact gate, not a claim of Windows XP runtime validation.
Uses only Python's standard library so it can run on CI and developer machines.
"""
from pathlib import Path
import argparse
import struct
import sys


def inspect(path):
    data = path.read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError('truncated PE structure')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError('not an MZ image')
    pe, = unpack('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('missing PE signature')
    coff = pe + 4
    machine, section_count = unpack('<HH', coff)
    optional_size, characteristics = unpack('<HH', coff + 16)
    optional = coff + 20
    magic, = unpack('<H', optional)
    if machine != 0x14c or magic != 0x10b:
        raise ValueError('expected IMAGE_FILE_MACHINE_I386 and PE32')
    if not (characteristics & 0x2000) or not (characteristics & 0x2):
        raise ValueError('expected an executable DLL image')
    if optional_size < 224:
        raise ValueError('truncated PE32 optional header')
    os_version = unpack('<HH', optional + 40)
    subsystem_version = unpack('<HH', optional + 48)
    subsystem, = unpack('<H', optional + 68)
    if os_version != (5, 1) or subsystem_version != (5, 1):
        raise ValueError('expected OS/subsystem 5.1, got %s / %s' %
                         (os_version, subsystem_version))
    if subsystem != 2:
        raise ValueError('expected Windows GUI subsystem')
    headers_size, = unpack('<I', optional + 60)
    directory_count, = unpack('<I', optional + 92)
    if directory_count < 14:
        raise ValueError('missing import data directories')
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        section = section_table + index * 40
        virtual_size, rva, raw_size, raw_offset = unpack('<IIII', section + 8)
        sections.append((rva, virtual_size, raw_offset, raw_size))

    def file_offset(rva):
        if rva < headers_size and rva < len(data):
            return rva
        for base, virtual_size, raw_offset, raw_size in sections:
            if base <= rva < base + max(virtual_size, raw_size):
                relative = rva - base
                if relative >= raw_size or raw_offset + relative >= len(data):
                    raise ValueError('RVA points outside file-backed section data')
                return raw_offset + relative
        raise ValueError('RVA outside image sections')

    def string_at(rva):
        offset = file_offset(rva)
        end = data.find(b'\0', offset)
        if end < 0:
            raise ValueError('unterminated import name')
        return data[offset:end].decode('ascii')

    delay_rva, delay_size = unpack('<II', optional + 96 + 13 * 8)
    if delay_rva or delay_size:
        raise ValueError('delay imports require explicit review; none are expected')
    import_rva, import_size = unpack('<II', optional + 96 + 8)
    imports = {}
    if import_rva:
        terminated = False
        for index in range(import_size // 20):
            descriptor = unpack('<IIIII', file_offset(import_rva + index * 20))
            if descriptor == (0, 0, 0, 0, 0):
                terminated = True
                break
            original_thunk, _, _, name_rva, first_thunk = descriptor
            module = string_at(name_rva).lower()
            if module not in {'kernel32.dll', 'user32.dll'}:
                raise ValueError('unexpected import DLL (CRT-free contract): ' + module)
            names = []
            thunk = original_thunk or first_thunk
            for symbol_index in range(len(data) // 4):
                entry, = unpack('<I', file_offset(thunk + symbol_index * 4))
                if not entry:
                    break
                if entry & 0x80000000:
                    raise ValueError('ordinal import requires explicit review: ' + module)
                names.append(string_at(entry + 2))
            else:
                raise ValueError('unterminated import thunk table')
            imports.setdefault(module, []).extend(names)
        if not terminated:
            raise ValueError('unterminated import descriptor table')
    elif import_size:
        raise ValueError('import size without an import RVA')
    return imports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path)
    args = parser.parse_args()
    try:
        imports = inspect(args.dll)
    except (OSError, ValueError, struct.error) as exc:
        print('FAIL PE contract: ' + str(exc), file=sys.stderr)
        return 1
    print('PASS PE32/x86 DLL; OS 5.1; subsystem 5.1; CRT-free import DLLs')
    for module, names in sorted(imports.items()):
        print('  %s: %s' % (module, ', '.join(sorted(names))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
