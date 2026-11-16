#!/usr/bin/env python3
"""Fail CI if any required CipherSNES v0.1 artwork or release markers are stale."""
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'assets'
master = (ASSETS / 'CipherSNES_icon.png').read_bytes()
assert master[:8] == b'\x89PNG\r\n\x1a\n', 'Missing new main PNG'
w, h = struct.unpack('>II', master[16:24])
assert w == h and w >= 512, 'Invalid master artwork dimensions'
scaled = (ASSETS / 'CipherSNES_icon_256.png').read_bytes()
assert scaled[:8] == master[:8] and struct.unpack('>II', scaled[16:24]) == (256, 256)
ppm = (ASSETS / 'CipherSNES_about.ppm').read_bytes()
assert ppm.startswith(b'P6\n176 176\n255\n') and len(ppm) > 92000
ico = (ASSETS / 'CipherSNES.ico').read_bytes()
assert ico[:4] == b'\0\0\1\0' and struct.unpack('<H', ico[4:6])[0] >= 5
icns = (ASSETS / 'CipherSNES.icns').read_bytes()
assert icns[:4] == b'icns' and struct.unpack('>I', icns[4:8])[0] == len(icns)
assert '1 ICON "CipherSNES.ico"' in (ASSETS / 'CipherSNES.rc').read_text()
assert not (ASSETS / 'CipherSNES_about_screen.png').exists(), 'Stale old-brand screenshot retained'
cmake = (ROOT / 'CMakeLists.txt').read_text()
assert 'project(CipherSNES VERSION 0.1.0 ' in cmake
assert 'assets/CipherSNES.rc' in cmake and 'MACOSX_BUNDLE_ICON_FILE "CipherSNES.icns"' in cmake
assert 'assets/CipherSNES_icon.png' in (ROOT / 'README.md').read_text()
for path in (ROOT / '.github/workflows').glob('*.yml'):
    if path.name.startswith(('release-', 'portability-')):
        assert '0.2' not in path.read_text(), path.name
assert not list((ROOT / '.github/workflows').glob('*v0.2*'))
print('CipherSNES v0.1 branding resources and release markers: PASS')
