"""Check real Mac shortcut bundles with synthetic artwork and a scratch HOME."""
from pathlib import Path
import os
import plistlib
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="Fulldex's shortcuts ") as tmp:
    home = Path(tmp)
    (home / 'Desktop').mkdir()
    exe = home / 'Fulldex test'
    subprocess.run(['c++', '-std=c++17', '-I' + str(ROOT / 'frontend'),
                    str(ROOT / 'tests/mac_shortcuts.cpp'), '-o', str(exe)], check=True)
    env = dict(os.environ, HOME=str(home))
    result = subprocess.check_output([str(exe)], env=env, text=True)
    assert result.startswith('1 ON YOUR DESKTOP AND IN YOUR APPS'), result
    for base in ['Applications', 'Desktop']:
        apps = list((home / base).glob('*.app'))
        assert len(apps) == 1, apps
        app = apps[0]
        plist = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
        assert plist['CFBundleIdentifier'].endswith('.ireo')
        launch = app / 'Contents/MacOS' / plist['CFBundleExecutable']
        assert subprocess.check_output([str(launch)], env=env, text=True).splitlines() == ['--game', 'IREO']
        icon = (app / 'Contents/Resources/icon.icns').read_bytes()
        assert icon[:4] == b'icns' and struct.unpack('>I', icon[4:8])[0] == len(icon)
        at = 8
        for kind, size in [(b'ic07', 128), (b'ic08', 256), (b'ic09', 512)]:
            assert icon[at:at+4] == kind
            length = struct.unpack('>I', icon[at+4:at+8])[0]
            png = icon[at+8:at+length]
            assert png[:8] == b'\x89PNG\r\n\x1a\n'
            assert struct.unpack('>II', png[16:24]) == (size, size)
            at += length
        assert at == len(icon)
    print('Mac shortcut bundles, icons, duplicate handling and launch arguments passed')
