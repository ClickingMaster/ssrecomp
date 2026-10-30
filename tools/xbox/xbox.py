"""The Xbox build: the game as a UWP app, compiled with clang-cl and packed into an MSIX.

The Xbox only starts UWP apps (in Developer Mode, from a package installed through its
Device Portal), so this needs Visual Studio with these parts (Visual Studio Installer ->
Modify):
  - "Desktop development with C++", with "C++ Clang tools for Windows"
  - "Windows application development" (UWP), with "C++ (v143) Universal Windows
    Platform tools"
SDL2 is compiled for UWP with Visual Studio's own compiler (its UWP code is C++/CX, which
only that compiler understands); the game itself with clang-cl.

Used by ssrecomp_setup.py (Build for Xbox, or --xbox). The package does not hold the ROM
(the player copies it to the console, see main.cpp), but its code is recompiled from it,
so it must not be shared either.
"""
import glob
import json
import os
import re
import secrets
import shutil
import struct
import subprocess
import zlib

# The package's identity. The publisher must be the subject of the certificate it is
# signed with (made on this computer, below).
PACKAGE_NAME = 'io.github.clickingmaster.ssrecomp'
PUBLISHER = 'CN=ssrecomp'
VCLIBS = 'Microsoft.VCLibs.140.00'
VCLIBS_PUBLISHER = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'

VS_HELP = ('Install Visual Studio 2022 (the free Community edition is enough) and in the Visual '
           'Studio Installer choose "Desktop development with C++" with "C++ Clang tools for '
           'Windows", and "Windows application development" (UWP) with "C++ (v143) Universal '
           'Windows Platform tools".')


VS_YEARS = {'16': '2019', '17': '2022', '18': '2026'}


def find_visual_studio(uwp=True):
    """The newest Visual Studio with clang-cl (and the UWP C++ libraries), as a dict;
    raises RuntimeError saying what to install."""
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.exists(vswhere):
        raise RuntimeError('Visual Studio was not found.\n' + VS_HELP)
    out = subprocess.run([vswhere, '-products', '*', '-format', 'json', '-utf8', '-sort'],
                         capture_output=True, text=True, encoding='utf-8')
    found = json.loads(out.stdout or '[]')
    problems = []
    for vs in found:
        root = vs.get('installationPath', '')
        vc = os.path.join(root, 'VC')
        clang = os.path.join(vc, 'Tools', 'Llvm', 'x64', 'bin', 'clang-cl.exe')
        store = glob.glob(os.path.join(vc, 'Tools', 'MSVC', '*', 'lib', 'x64', 'store', 'msvcrt.lib'))
        if not os.path.exists(clang):
            problems.append(root + ': no "C++ Clang tools for Windows"')
            continue
        if uwp and not store:
            problems.append(root + ': no "C++ Universal Windows Platform tools"')
            continue
        major = vs.get('installationVersion', '0').split('.')[0]
        # CMake's generator names pair the major version with the release year; the
        # installer's own "product line version" is not always that year (2026 says 18).
        year = VS_YEARS.get(major) or vs.get('catalog', {}).get('productLineVersion', '')
        return {
            'root': root,
            'vcvarsall': os.path.join(vc, 'Auxiliary', 'Build', 'vcvarsall.bat'),
            'clang_cl': clang,
            'lld_link': os.path.join(os.path.dirname(clang), 'lld-link.exe'),
            'generator': f'Visual Studio {major} {year}',
        }
    raise RuntimeError('Visual Studio is missing parts the Xbox build needs' +
                       (' (' + '; '.join(problems) + ')' if problems else '') + '.\n' + VS_HELP)


def uwp_env(vs):
    """The environment of a "vcvarsall x64 uwp" prompt: the compilers, the Windows SDK's
    tools (makeappx, signtool) and the UWP version of the C++ runtime libraries."""
    out = subprocess.run(f'"{vs["vcvarsall"]}" x64 uwp >nul && set', shell=True,
                         capture_output=True, text=True, errors='replace')
    env = {}
    for line in out.stdout.splitlines():
        name, sep, value = line.partition('=')
        if sep and name:
            env[name] = value
    if out.returncode != 0 or 'VCToolsInstallDir' not in env:
        raise RuntimeError('Could not set up the Visual Studio environment (vcvarsall x64 uwp):\n' +
                           (out.stderr or out.stdout)[-2000:])
    return env


def which(name, env):
    """A program found on the PATH of env (not of this process)."""
    path = shutil.which(name, path=env.get('Path') or env.get('PATH'))
    if not path:
        raise RuntimeError(name + ' was not found in the Visual Studio environment.\n' + VS_HELP)
    return path


def pe_imports(path):
    """The DLLs a Windows program imports (from its PE import table)."""
    with open(path, 'rb') as f:
        data = f.read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    nsections = struct.unpack_from('<H', data, pe + 6)[0]
    opt_size = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    import_rva = struct.unpack_from('<I', data, opt + 120)[0]  # PE32+: data directory 1
    sections = []
    for i in range(nsections):
        s = pe + 24 + opt_size + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
        sections.append((va, max(vsize, rsize), raw))

    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError('bad RVA')
    names = []
    d = offset(import_rva)
    while True:
        name_rva = struct.unpack_from('<I', data, d + 12)[0]
        if not name_rva:
            break
        o = offset(name_rva)
        names.append(data[o:data.index(b'\0', o)].decode('ascii'))
        d += 20
    return names


def check_imports(exe):
    """The desktop C++ runtime is not there on the Xbox; the program must use the UWP one
    (vcruntime140_app.dll), which comes from the Microsoft.VCLibs package."""
    desktop = [n for n in pe_imports(exe) if re.fullmatch(r'(?i)(vcruntime140(_1)?|msvcp140(_\d)?)\.dll', n)]
    if desktop:
        raise RuntimeError('The game was linked with the desktop C++ runtime (' + ', '.join(desktop) +
                           ') instead of the UWP one; ' + VS_HELP)


# ---------------------------------------------------------------------------
# Tiles and icons, made from the setup program's icon.

def read_png(path):
    """An RGBA, non-interlaced PNG (8 or 16 bits) as (width, height, rows of 8-bit RGBA)."""
    with open(path, 'rb') as f:
        data = f.read()
    pos, idat = 8, b''
    while pos < len(data):
        length, kind = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            w, h, depth, colour, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth not in (8, 16) or colour != 6 or interlace:
                raise ValueError('only RGBA PNGs are read')
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    bpp = depth // 2  # bytes per pixel
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line if depth == 8 else line[::2])  # 16 bits: the high bytes
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def tile(icon, w, h, size):
    """The icon scaled to size x size (averaging, with alpha), centred on a transparent
    w x h picture."""
    iw, ih, src = icon
    rows = [bytearray(w * 4) for _ in range(h)]
    x0, y0 = (w - size) // 2, (h - size) // 2
    for y in range(size):
        sy0, sy1 = y * ih // size, max(y * ih // size + 1, (y + 1) * ih // size)
        for x in range(size):
            sx0, sx1 = x * iw // size, max(x * iw // size + 1, (x + 1) * iw // size)
            acc = [0, 0, 0, 0]
            for sy in range(sy0, sy1):
                line = src[sy]
                for sx in range(sx0, sx1):
                    r, g, b, a = line[sx * 4:sx * 4 + 4]
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = (sy1 - sy0) * (sx1 - sx0)
            px = [acc[i] // acc[3] for i in range(3)] + [acc[3] // n] if acc[3] else [0, 0, 0, 0]
            rows[y0 + y][(x0 + x) * 4:(x0 + x) * 4 + 4] = bytes(px)
    return rows


ASSETS = {  # file: (width, height, icon size)
    'Square44x44Logo.png': (44, 44, 44),
    'Square150x150Logo.png': (150, 150, 120),
    'Wide310x150Logo.png': (310, 150, 120),
    'StoreLogo.png': (50, 50, 50),
    'SplashScreen.png': (620, 300, 240),
}


def write_assets(icon_path, folder):
    os.makedirs(folder, exist_ok=True)
    icon = read_png(icon_path)
    for name, (w, h, size) in ASSETS.items():
        write_png(os.path.join(folder, name), w, h, tile(icon, w, h, size))


def powershell(script):
    """Runs script in Windows PowerShell. Without PowerShell 7's module path: started from
    PowerShell 7, Windows PowerShell inherits it and cannot load its own modules (then the
    Cert: drive, among others, is missing)."""
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PSMODULEPATH'}
    return subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', script],
                          capture_output=True, text=True, errors='replace', env=env)


def write_assets_from_picture(picture, folder):
    """The tiles from the player's own picture (PNG, JPEG, BMP...: anything Windows reads),
    each as large as it fits, keeping its shape. Scaled by Windows (System.Drawing), which
    reads the formats this file's PNG reader does not."""
    os.makedirs(folder, exist_ok=True)
    # "file|width|height" each (nested arrays would be flattened by PowerShell).
    jobs = ','.join(f'"{os.path.join(folder, n)}|{w}|{h}"' for n, (w, h, _) in ASSETS.items())
    script = (
        '$ErrorActionPreference = "Stop"; Add-Type -AssemblyName System.Drawing;'
        f'$src = [System.Drawing.Image]::FromFile("{os.path.abspath(picture)}");'
        f'foreach ($j in @({jobs})) {{'
        '  $f = $j.Split("|"); $w = [int]$f[1]; $h = [int]$f[2];'
        '  $s = [Math]::Min($w / $src.Width, $h / $src.Height);'
        '  $dw = [int]($src.Width * $s); $dh = [int]($src.Height * $s);'
        '  $bmp = New-Object System.Drawing.Bitmap $w, $h;'
        '  $g = [System.Drawing.Graphics]::FromImage($bmp);'
        '  $g.InterpolationMode = "HighQualityBicubic"; $g.Clear([System.Drawing.Color]::Transparent);'
        '  $g.DrawImage($src, [int](($w - $dw) / 2), [int](($h - $dh) / 2), $dw, $dh);'
        '  $bmp.Save($f[0], [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose() };'
        '$src.Dispose()')
    out = powershell(script)
    if out.returncode != 0:
        raise RuntimeError('Could not make the tiles from ' + picture + ':\n' + (out.stderr or out.stdout)[-2000:])


def custom_icon(key_dir):
    """The player's own picture for the app's tiles, if they put one in key_dir
    (icon.png, icon.jpg...)."""
    for ext in ('png', 'jpg', 'jpeg', 'bmp'):
        path = os.path.join(key_dir, 'icon.' + ext)
        if os.path.isfile(path):
            return path
    return None


# ---------------------------------------------------------------------------
# The package.

def manifest(version):
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap rescap">
  <Identity Name="{PACKAGE_NAME}" Publisher="{PUBLISHER}" Version="{version}" ProcessorArchitecture="x64"/>
  <Properties>
    <DisplayName>SoulSilver</DisplayName>
    <PublisherDisplayName>ssrecomp</PublisherDisplayName>
    <Logo>Assets\\StoreLogo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.17763.0" MaxVersionTested="10.0.22621.0"/>
    <PackageDependency Name="{VCLIBS}" MinVersion="14.0.30035.0" Publisher="{VCLIBS_PUBLISHER}"/>
  </Dependencies>
  <Resources>
    <Resource Language="en-us"/>
  </Resources>
  <Applications>
    <Application Id="App" Executable="ssrecomp.exe" EntryPoint="ssrecomp.App">
      <uap:VisualElements DisplayName="SoulSilver" Description="Pokemon SoulSilver, recompiled (ssrecomp)"
                          BackgroundColor="transparent" Square150x150Logo="Assets\\Square150x150Logo.png"
                          Square44x44Logo="Assets\\Square44x44Logo.png">
        <uap:DefaultTile Wide310x150Logo="Assets\\Wide310x150Logo.png"/>
        <uap:SplashScreen Image="Assets\\SplashScreen.png" BackgroundColor="#000000"/>
      </uap:VisualElements>
    </Application>
  </Applications>
  <Capabilities>
    <!-- For the ROM in the Xbox's DevelopmentFiles folder (see main.cpp). -->
    <rescap:Capability Name="broadFileSystemAccess"/>
  </Capabilities>
</Package>
'''


def next_version(release, key_dir):
    """release ("0.4.2") plus a build number kept in key_dir (or SSR_BUILD_NUMBER, as the
    launcher workflow sets it): a package installs over an older one only with a higher
    version."""
    if os.environ.get('SSR_BUILD_NUMBER', '').isdigit():
        n = int(os.environ['SSR_BUILD_NUMBER'])
        parts = (release.split('.') + ['0', '0', '0'])[:3]
        return '.'.join(parts + [str(n % 65536)])
    counter = os.path.join(key_dir, 'build-number.txt')
    try:
        with open(counter) as f:
            n = int(f.read().strip()) + 1
    except (OSError, ValueError):
        n = 1
    with open(counter, 'w') as f:
        f.write(str(n))
    parts = (release.split('.') + ['0', '0', '0'])[:3]
    return '.'.join(parts + [str(n % 65536)])


def signing_key(key_dir, log):
    """A self-signed code-signing certificate for PUBLISHER, made once and kept, so that
    later packages install over earlier ones. Returns (pfx, password, cer)."""
    pfx, cer, pw_file = (os.path.join(key_dir, n) for n in ('signing-key.pfx', 'signing-key.cer', 'signing-key.txt'))
    if os.path.exists(pfx) and os.path.exists(pw_file) and os.path.exists(cer):
        with open(pw_file) as f:
            return pfx, f.read().strip(), cer
    password = secrets.token_hex(16)
    log('Making the signing certificate ' + pfx)
    script = (
        '$ErrorActionPreference = "Stop";'
        f'$c = New-SelfSignedCertificate -Type Custom -Subject "{PUBLISHER}" -KeyUsage DigitalSignature '
        '-FriendlyName "ssrecomp package signing" -CertStoreLocation "Cert:\\CurrentUser\\My" '
        '-TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}");'
        f'$p = ConvertTo-SecureString -String "{password}" -Force -AsPlainText;'
        f'Export-PfxCertificate -Cert $c -FilePath "{pfx}" -Password $p | Out-Null;'
        f'Export-Certificate -Cert $c -FilePath "{cer}" | Out-Null;'
        'Remove-Item -Path ("Cert:\\CurrentUser\\My\\" + $c.Thumbprint)')
    out = powershell(script)
    if out.returncode != 0 or not os.path.exists(pfx):
        raise RuntimeError('Could not make the signing certificate:\n' + (out.stderr or out.stdout)[-2000:])
    with open(pw_file, 'w') as f:
        f.write(password)
    return pfx, password, cer


def vclibs_package():
    """Microsoft's C++ runtime package for UWP apps, which Visual Studio's UWP tools
    install; the Xbox needs it installed once next to the game."""
    base = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft SDKs',
                        'Windows Kits', '10', 'ExtensionSDKs', 'Microsoft.VCLibs', '14.0', 'Appx', 'Retail', 'x64')
    found = glob.glob(os.path.join(base, 'Microsoft.VCLibs.x64.14.00.appx'))
    return found[0] if found else None


def package(exe, sdl_dll, icon, release, out_dir, key_dir, env, run, log, rom=None, picture=None):
    """Packs exe and SDL2.dll (and rom, if given) into a signed MSIX in out_dir; returns
    its path. The tiles come from picture (any image, see write_assets_from_picture) or
    else from icon (an RGBA PNG)."""
    check_imports(exe)
    os.makedirs(key_dir, exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    layout = os.path.join(os.path.dirname(exe), 'msix-layout')
    shutil.rmtree(layout, ignore_errors=True)
    os.makedirs(layout)
    shutil.copy2(exe, os.path.join(layout, 'ssrecomp.exe'))
    shutil.copy2(sdl_dll, os.path.join(layout, 'SDL2.dll'))
    if rom:
        shutil.copyfile(rom, os.path.join(layout, 'rom.nds'))
    if picture:
        log('Tiles from ' + picture)
        write_assets_from_picture(picture, os.path.join(layout, 'Assets'))
    else:
        write_assets(icon, os.path.join(layout, 'Assets'))
    version = next_version(release or '0.0.0', key_dir)
    with open(os.path.join(layout, 'AppxManifest.xml'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(manifest(version))
    log('Package version ' + version)

    msix = os.path.join(out_dir, 'SoulSilver-ssrecomp.msix')
    run([which('makeappx.exe', env), 'pack', '/o', '/d', layout, '/p', msix], env=env)
    pfx, password, cer = signing_key(key_dir, log)
    run([which('signtool.exe', env), 'sign', '/fd', 'SHA256', '/f', pfx, '/p', password, msix], env=env)
    shutil.copy2(cer, os.path.join(out_dir, 'SoulSilver-ssrecomp.cer'))
    vclibs = vclibs_package()
    if vclibs:
        shutil.copy2(vclibs, os.path.join(out_dir, os.path.basename(vclibs)))
    # The unpacked copy may hold the player's ROM; only the package is kept.
    shutil.rmtree(layout, ignore_errors=True)
    return msix
