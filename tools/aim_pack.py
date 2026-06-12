#!/usr/bin/env python3
"""Pack files into a self-extracting .aim archive for AaronOS installer.

Usage: python3 aim_pack.py output.aim name1:path1 name2:path2 ...
"""
import struct
import sys

if len(sys.argv) < 3:
    print("Usage: aim_pack.py output.aim name:path [name:path ...]")
    sys.exit(1)

outpath = sys.argv[1]
files = []
for arg in sys.argv[2:]:
    name, path = arg.split(':', 1)
    with open(path, 'rb') as f:
        data = f.read()
    files.append((name, data))

with open(outpath, 'wb') as f:
    f.write(b'AIM1')
    for name, data in files:
        name_bytes = name.encode('ascii')
        f.write(struct.pack('<I', len(name_bytes)))
        f.write(name_bytes)
        f.write(struct.pack('<I', len(data)))
        f.write(data)
    f.write(struct.pack('<I', 0))

print(f"Created {outpath} with {len(files)} file(s) ({sum(len(d) for _,d in files)} bytes total)")
