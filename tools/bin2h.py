#!/usr/bin/env python3
"""bin2h.py ROM.BIN out.h - embed the MSX test ROM in the firmware"""
import sys
data = open(sys.argv[1], 'rb').read()
assert len(data) == 16384, len(data)
with open(sys.argv[2], 'w') as f:
    f.write('// generated from %s by tools/bin2h.py - do not edit\n#pragma once\n#include <stdint.h>\n' % sys.argv[1].split('/')[-1])
    f.write('static const uint8_t msx_rom[16384] = {\n')
    for i in range(0, len(data), 16):
        f.write('  ' + ','.join('0x%02X' % b for b in data[i:i+16]) + ',\n')
    f.write('};\n')
