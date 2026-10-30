#!/usr/bin/env python3
"""ssrecomp setup: builds the game from your own copy of Pokemon SoulSilver (US).

Pick your ROM, press Build, and play. The ROM's code is recompiled to C++ on your
machine (nothing of the game is downloaded or shipped) and compiled together with
the frontend. Needs Python 3 with Tk, CMake, Ninja, a C++ compiler and the SDL2,
libpng and zlib development files; the window says what is missing and how to get it.

Without a display: python3 ssrecomp_setup.py --rom <file> [--jobs N] [--android | --xbox [--xbox-icon <picture>]]
The packaged program updates itself with --update.
"""
import glob
import hashlib
import json
import os
import platform
import site
import sysconfig
import tempfile
from functools import lru_cache
import queue
import re
import shlex
import shutil
import subprocess
import sys
import threading

WINDOWS = os.name == 'nt'
DARWIN = sys.platform == 'darwin'
MAC_ARCH = platform.machine() if DARWIN else None
ANDROID_MAC_NOTE = 'Android builds from macOS are not supported yet; use a Linux or Windows host.'
# Packaged with tools/package_setup.py, the project's source travels
# inside it and is unpacked into a folder of its own, where the game is then built.
FROZEN = getattr(sys, 'frozen', False)


def self_bundle_path():
    """If running inside a macOS .app bundle, return the path to the bundle directory."""
    if not DARWIN:
        return None
    p = os.path.abspath(sys.executable)
    while p and p != os.path.dirname(p):
        if p.endswith('.app'):
            return p
        p = os.path.dirname(p)
    return None


def self_path():
    """The file or bundle to replace: the AppImage on Linux, .app bundle on macOS, else the executable."""
    return os.environ.get('APPIMAGE') or self_bundle_path() or sys.executable


# Portable mode: with a file portable.txt next to the executable (the AppImage on Linux,
# the .app bundle on macOS), everything is kept in the folder ssrecomp-data beside it
# instead of the user's profile.
EXE_DIR = os.path.dirname(os.path.abspath(self_path())) if FROZEN else None
PORTABLE = FROZEN and os.path.exists(os.path.join(EXE_DIR, 'portable.txt'))
if PORTABLE:
    HERE = os.path.join(EXE_DIR, 'ssrecomp-data')
elif FROZEN:
    if WINDOWS:
        HERE = os.path.join(os.environ.get('LOCALAPPDATA', os.path.expanduser('~')), 'ssrecomp')
    elif DARWIN:
        HERE = os.path.expanduser('~/Library/Application Support/ssrecomp')
    else:
        HERE = os.path.join(os.environ.get('XDG_DATA_HOME', os.path.expanduser('~/.local/share')), 'ssrecomp')
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(HERE, 'layouts', 'soulsilver-us.json')
GEN = os.path.join(HERE, 'gen')
BUILD = os.path.join(HERE, 'build-macos-' + MAC_ARCH if DARWIN else 'build')
EXE = os.path.join(BUILD, 'ssrecomp.exe' if WINDOWS else 'ssrecomp')
ROM_NOTE = os.path.join(HERE, 'ssrecomp.rom')

# The Android build: Google's NDK (compilers for Android) and SDL2's source are downloaded
# into ANDROID_DIR the first time; the result is the APK at ANDROID_APK.
ANDROID_DIR = os.path.join(HERE, 'android')
ANDROID_APK = os.path.join(HERE, 'SoulSilver-ssrecomp.apk')
ANDROID_ABI = 'arm64-v8a'          # phones and handhelds from the last ten years
ANDROID_PLATFORM = 'android-24'    # Android 7.0 and later
NDK_NAME = 'android-ndk-r27c'
NDK_DOWNLOADS = {   # host -> (url, sha1, size), from Google's repository listing
    'linux': ('https://dl.google.com/android/repository/android-ndk-r27c-linux.zip',
              '090e8083a715fdb1a3e402d0763c388abb03fb4e', 663987688),
    'windows': ('https://dl.google.com/android/repository/android-ndk-r27c-windows.zip',
                'ac5f7762764b1f15341094e148ad4f847d050c38', 781511249),
}
# The Xbox build (Windows only, tools/xbox/xbox.py): SDL2 for UWP and the signing key go
# into XBOX_DIR; the MSIX (with the player's ROM in it) into XBOX_OUT.
XBOX_DIR = os.path.join(HERE, 'xbox')
XBOX_OUT = os.path.join(HERE, 'xbox-package')
XBOX_WINDOWS_NOTE = 'The Xbox build needs Windows (with Visual Studio).'
SDL2_NAME = 'SDL2-2.32.10'
SDL2_DOWNLOAD = ('https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz',
                 '5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165')


# ---------------------------------------------------------------------------
# The steps, usable without the window.

def child_env():
    """The environment for programs this starts. A packaged executable points the
    library path at its own bundled libraries; the compiler, pkg-config and the game
    must not see those."""
    env = dict(os.environ)
    if FROZEN and not WINDOWS:
        orig = env.pop('LD_LIBRARY_PATH_ORIG', None)
        if orig is not None:
            env['LD_LIBRARY_PATH'] = orig
        else:
            env.pop('LD_LIBRARY_PATH', None)
    return env


def unpack():
    """Packaged: puts the project's source into HERE (again when this executable is a
    different version), leaving what was built there alone."""
    if not FROZEN:
        return
    stamp_path = os.path.join(HERE, '.bundle')
    with open(os.path.join(sys._MEIPASS, 'bundle.id')) as f:
        stamp = f.read()
    if os.path.exists(stamp_path):
        with open(stamp_path) as f:
            if f.read() == stamp:
                return
    for item in os.listdir(os.path.join(sys._MEIPASS, 'project')):
        src, dst = os.path.join(sys._MEIPASS, 'project', item), os.path.join(HERE, item)
        if os.path.isdir(src):
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            os.makedirs(HERE, exist_ok=True)
            shutil.copy2(src, dst)
    with open(stamp_path, 'w') as f:
        f.write(stamp)


def read_rom_note():
    """The ROM recorded by the last build. In portable mode it is kept relative to HERE
    when it can be, so that it is still found when the folder moves (another drive letter)."""
    with open(ROM_NOTE) as f:
        rom = f.read().strip()
    return os.path.normpath(os.path.join(HERE, rom)) if rom and not os.path.isabs(rom) else rom


def write_rom_note(rom):
    rom = os.path.abspath(rom)
    if PORTABLE:
        try:
            rom = os.path.relpath(rom, HERE)
        except ValueError:   # on another drive than HERE: only an absolute path works
            pass
    with open(ROM_NOTE, 'w') as f:
        f.write(rom + '\n')


def find_msys2():
    """On Windows, the compiler, CMake and libraries from MSYS2 are used even when this
    was not started from an MSYS2 window, and ahead of other copies of CMake or a
    compiler on the PATH (those cannot find MSYS2's libraries)."""
    if not WINDOWS:
        return
    for root in (os.environ.get('MSYS2_ROOT', ''), r'C:\msys64', r'C:\tools\msys64'):
        bindir = os.path.join(root, 'ucrt64', 'bin')
        if root and os.path.exists(os.path.join(bindir, 'g++.exe')):
            os.environ['PATH'] = bindir + os.pathsep + os.environ['PATH']
            return


def find_macos_tools():
    """Finder does not inherit a terminal's Homebrew PATH; prefer this process's arch."""
    if not DARWIN:
        return
    prefixes = ['/opt/homebrew', '/usr/local'] if MAC_ARCH == 'arm64' else ['/usr/local', '/opt/homebrew']
    # Honor an explicit PATH first, then add both standard Homebrew installations.
    paths = os.environ.get('PATH', os.defpath).split(os.pathsep)
    paths += [sysconfig.get_path('scripts'), os.path.join(site.getuserbase(), 'bin')]
    paths += ['/Library/Frameworks/Python.framework/Versions/Current/bin']
    paths += sorted(glob.glob(os.path.expanduser('~/Library/Python/*/bin')), reverse=True)
    paths += [p + '/bin' for p in prefixes] + ['/usr/bin', '/bin', '/usr/sbin', '/sbin']
    os.environ['PATH'] = os.pathsep.join(dict.fromkeys(paths))
    os.environ.setdefault('CC', 'clang')
    os.environ.setdefault('CXX', 'clang++')


@lru_cache(maxsize=4)
def macos_toolchain_problem(path, cxx):
    """Check the compiler and SDK before generating the game.

    Cache by tool environment so the UI can refresh without repeatedly invoking clang.
    """
    try:
        compiler = shlex.split(cxx) if cxx else ['xcrun', 'clang++']
        with tempfile.TemporaryDirectory(prefix='ssrecomp-toolchain-') as tmp:
            source = os.path.join(tmp, 'probe.cpp')
            with open(source, 'w') as f:
                f.write('#include <thread>\n#include <zlib.h>\n'
                        'int main() { std::thread t([] {}); t.join(); return zlibVersion() == nullptr; }\n')
            cmd = [*compiler, '-std=c++17', '-arch', MAC_ARCH, '-mmacosx-version-min=11.0', source,
                   '-lz', '-o', os.path.join(tmp, 'probe')]
            result = subprocess.run(cmd, env=child_env(), text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30)
            if result.returncode:
                return ('macOS compiler/SDK check failed for ' + MAC_ARCH +
                        ':\n' + result.stdout[-2500:])
    except (OSError, subprocess.SubprocessError) as e:
        return 'macOS compiler/SDK check failed: ' + str(e)
    return None


def windows_prefix():
    """The MSYS2 environment (e.g. C:\\msys64\\ucrt64) the compiler belongs to, or None."""
    gxx = shutil.which('g++')
    if not WINDOWS or not gxx:
        return None
    return os.path.dirname(os.path.dirname(os.path.abspath(gxx)))


def check_rom(path):
    """Returns None if this is the right game, otherwise what is wrong with it."""
    if not os.path.isfile(path):
        return 'That file does not exist.'
    layout = json.load(open(LAYOUT))
    digest = hashlib.sha1(open(path, 'rb').read()).hexdigest()
    if digest == layout['rom_sha1']:
        return None
    # A dump with a different header or padding can still hold the same code; the
    # recompiler checks every code section, so let it decide.
    return ('This is not the exact ROM the layout was made for (SHA-1 %s, expected %s). '
            'It will still be used if its code is identical.' % (digest, layout['rom_sha1']))


def missing_tools():
    """What is needed to compile and is not installed, as (name, how to get it) pairs."""
    missing = []
    for tool in ('cmake', 'ninja'):
        if not shutil.which(tool):
            missing.append(tool)
    if not (shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')):
        missing.append('a C++ compiler')
    if not WINDOWS and not DARWIN and shutil.which('pkg-config'):
        for lib, name in (('sdl2', 'SDL2'), ('libpng', 'libpng'), ('zlib', 'zlib')):
            if subprocess.run(['pkg-config', '--exists', lib], env=child_env()).returncode != 0:
                missing.append(name + ' (development files)')
    elif not WINDOWS and not DARWIN:
        missing.append('pkg-config')
    prefix = windows_prefix()
    if prefix:
        for path, name in ((os.path.join('lib', 'cmake', 'SDL2'), 'SDL2'), (os.path.join('include', 'png.h'), 'libpng'),
                           (os.path.join('include', 'zlib.h'), 'zlib')):
            if not os.path.exists(os.path.join(prefix, path)):
                missing.append(name)
    if DARWIN and not missing:
        problem = macos_toolchain_problem(os.environ.get('PATH', ''), os.environ.get('CXX', ''))
        if problem:
            missing.append(problem)
    return missing


def os_id():
    """The Linux distribution's ID from /etc/os-release (e.g. 'steamos', 'arch'), or ''."""
    try:
        for line in open('/etc/os-release'):
            if line.startswith('ID='):
                return line[3:].strip().strip('"')
    except OSError:
        pass
    return ''


def install_hint():
    if DARWIN:
        return ('Install Xcode Command Line Tools and Python 3 with Tk from https://www.python.org/downloads/macos/, '
                'then run:\n'
                '  xcode-select --install\n'
                '  python3 -m pip install "cmake>=3.21,<4" ninja\n'
                'CMake and Ninja can also be installed with Homebrew. '
                'SDL2 and libpng are downloaded and built automatically for macOS 11 or later; '
                'no Homebrew libraries or pkg-config are needed. Use native ' + MAC_ARCH + ' Python and tools.')
    if WINDOWS:
        return ('Install MSYS2 (https://www.msys2.org), open "MSYS2 UCRT64" and run:\n'
                '  pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake '
                'mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-libpng '
                'mingw-w64-ucrt-x86_64-zlib' + ('' if FROZEN else ' mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-tk') +
                '\nthen start this program again.')
    if os.path.exists('/run/ostree-booted') or os_id() == 'steamos':
        return ('This is an immutable system (Bazzite, Silverblue, SteamOS...), where programs are not installed '
                'directly. Open a terminal (Konsole on a Steam Deck or KDE) and set up a container with:\n'
                '  distrobox create -n ssrecomp -i ubuntu:24.04\n'
                '  distrobox enter ssrecomp -- sudo apt install -y ' + ('' if FROZEN else 'python3-tk ') + 'cmake ninja-build g++ '
                'pkg-config libsdl2-dev libpng-dev zlib1g-dev\n'
                '  distrobox enter ssrecomp -- ' + (shlex.quote(os.environ.get('APPIMAGE', sys.executable)) if FROZEN
                                                    else 'python3 ssrecomp_setup.py') + '\n'
                'The finished game also runs outside the container.')
    tk_apt, tk_dnf, tk_pac = ('', '', '') if FROZEN else ('python3-tk ', 'python3-tkinter ', 'tk ')
    how = ('Open a terminal, run this command (it asks for your password), then close and start this '
           'program again:\n  ')
    if shutil.which('apt-get'):
        return how + 'sudo apt install ' + tk_apt + 'cmake ninja-build g++ pkg-config libsdl2-dev libpng-dev zlib1g-dev'
    if shutil.which('dnf'):
        return how + 'sudo dnf install ' + tk_dnf + 'cmake ninja-build gcc-c++ pkgconf-pkg-config SDL2-devel libpng-devel zlib-devel'
    if shutil.which('pacman'):
        return how + 'sudo pacman -S --needed ' + tk_pac + 'cmake ninja gcc pkgconf sdl2 libpng zlib'
    return 'Install CMake, Ninja, a C++ compiler, pkg-config and the SDL2, libpng and zlib development files.'


def default_jobs():
    """Parallel compile jobs: one per CPU thread, but no more than the memory allows
    (each of the large generated files needs about 1.5 GB to compile)."""
    jobs = os.cpu_count() or 2
    try:
        if WINDOWS:
            import ctypes

            class MemoryStatus(ctypes.Structure):
                _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong),
                            ('ullTotalPhys', ctypes.c_ulonglong), ('ullAvailPhys', ctypes.c_ulonglong),
                            ('ullTotalPageFile', ctypes.c_ulonglong), ('ullAvailPageFile', ctypes.c_ulonglong),
                            ('ullTotalVirtual', ctypes.c_ulonglong), ('ullAvailVirtual', ctypes.c_ulonglong),
                            ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(MemoryStatus)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
            jobs = min(jobs, max(1, status.ullAvailPhys // (1500 * 1024 * 1024)))
        elif DARWIN:
            # Unified memory is shared with the GPU: reserve half and budget 2 GiB/job.
            memory = int(subprocess.check_output(['/usr/sbin/sysctl', '-n', 'hw.memsize'], text=True, stderr=subprocess.DEVNULL))
            jobs = min(jobs, max(1, memory // (4 * 1024**3)))
        else:
            for line in open('/proc/meminfo'):
                if line.startswith('MemAvailable:'):
                    jobs = min(jobs, max(1, int(line.split()[1]) // 1500000))
    except (OSError, AttributeError, ValueError, subprocess.SubprocessError):
        if DARWIN:
            jobs = min(jobs, 2)
    return max(1, int(jobs))


def download(url, digest, path, log, progress, label, algo='sha1'):
    """Downloads url to path (resuming a partial file) and checks its digest."""
    import urllib.request
    os.makedirs(os.path.dirname(path), exist_ok=True)
    part = path + '.part'
    if not os.path.exists(path):
        have = os.path.getsize(part) if os.path.exists(part) else 0
        req = urllib.request.Request(url, headers={'Range': 'bytes=%d-' % have} if have else {})
        log('Downloading ' + url)
        context = None
        try:
            # Packaged, Python may not find this system's certificates; certifi brings its own.
            import certifi
            import ssl
            context = ssl.create_default_context(cafile=certifi.where())
        except ImportError:
            pass
        with urllib.request.urlopen(req, context=context) as r, open(part, 'ab' if have and r.status == 206 else 'wb') as f:
            if r.status != 206:
                have = 0
            total = have + int(r.headers.get('Content-Length') or 0)
            done, last = have, -1
            while True:
                block = r.read(1 << 20)
                if not block:
                    break
                f.write(block)
                done += len(block)
                if total and done * 100 // total != last:
                    last = done * 100 // total
                    progress(None, '%s: %d of %d MB' % (label, done >> 20, total >> 20))
        os.replace(part, path)
    h = hashlib.new(algo)
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    if h.hexdigest() != digest:
        os.remove(path)
        raise RuntimeError('The download of %s was damaged (checksum mismatch). Try again.' % url)


def extract_zip(path, dest):
    """Unzips, keeping executable bits and symbolic links (Python's zipfile writes a
    link as a small file holding its target, and drops the bits)."""
    import stat
    import zipfile
    with zipfile.ZipFile(path) as z:
        for info in z.infolist():
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode) and not WINDOWS:
                out = os.path.join(dest, info.filename)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                if os.path.lexists(out):
                    os.remove(out)
                os.symlink(z.read(info).decode(), out)
                continue
            out = z.extract(info, dest)
            if mode and not WINDOWS:
                os.chmod(out, mode & 0o7777)


def android_ndk(log, progress):
    """The NDK: from ANDROID_NDK_HOME or ANDROID_NDK_ROOT when set, else downloaded."""
    if DARWIN:
        raise RuntimeError(ANDROID_MAC_NOTE)
    for var in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT'):
        ndk = os.environ.get(var)
        if ndk and os.path.exists(os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake')):
            return ndk
    ndk = os.path.join(ANDROID_DIR, NDK_NAME)
    if not os.path.exists(os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake')):
        url, sha1, size = NDK_DOWNLOADS['windows' if WINDOWS else 'linux']
        zip_path = os.path.join(ANDROID_DIR, 'downloads', os.path.basename(url))
        download(url, sha1, zip_path, log, progress, "Downloading Google's Android NDK (%d MB, once)" % (size >> 20))
        progress(None, 'Unpacking the NDK')
        extract_zip(zip_path, ANDROID_DIR)
        os.remove(zip_path)
    return ndk


def forget_moved_build(build):
    """CMake refuses a build folder that was configured somewhere else (the portable
    folder moved, or got another drive letter). Its configuration is then removed and made
    again; the game that is already built still runs without this."""
    cache = os.path.join(build, 'CMakeCache.txt')
    if not os.path.exists(cache):
        return
    with open(cache, errors='replace') as f:
        m = re.search(r'^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$', f.read(), re.M)
    if m and os.path.normcase(os.path.normpath(m.group(1))) != os.path.normcase(os.path.normpath(build)):
        os.remove(cache)
        shutil.rmtree(os.path.join(build, 'CMakeFiles'), ignore_errors=True)
        if os.path.exists(os.path.join(build, 'build.ninja')):
            os.remove(os.path.join(build, 'build.ninja'))


def run_steps(rom, jobs, log, progress, android=False, xbox=False, icon=None):
    """Generates and compiles (for Android: and makes the APK; for Xbox: the MSIX).
    log(text) reports a line, progress(fraction, label) the progress (fraction None: only
    the label changes). Raises RuntimeError with a message for the user on failure."""
    if DARWIN and android:
        raise RuntimeError(ANDROID_MAC_NOTE)
    if xbox and not WINDOWS:
        raise RuntimeError(XBOX_WINDOWS_NOTE)
    if jobs < 1:
        raise RuntimeError('Parallel compile jobs must be at least 1.')
    os.makedirs(HERE, exist_ok=True)

    def run(cmd, parse_ninja=False, env=None):
        if env is not None:
            # A program from that environment's PATH (Windows looks in this process's own).
            cmd = [shutil.which(cmd[0], path=env.get('Path') or env.get('PATH')) or cmd[0], *cmd[1:]]
        log('$ ' + shlex.join(cmd))
        proc = subprocess.Popen(cmd, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                errors='replace', env=child_env() if env is None else env)
        tail = []
        for line in proc.stdout:
            line = line.rstrip()
            tail = (tail + [line])[-30:]
            m = re.match(r'\[(\d+)/(\d+)\]', line) if parse_ninja else None
            if m:
                done, total = int(m.group(1)), int(m.group(2))
                progress(0.1 + 0.9 * done / total, f'Compiling ({done} of {total})')
                if done % 5 == 0 or done == total:
                    log(line)
            else:
                log(line)
        if proc.wait() != 0:
            raise RuntimeError('This step failed:\n' + ' '.join(cmd) + '\n\n' + '\n'.join(tail[-12:]))

    def tool(script, *args):
        # Run in this process: packaged as one executable there is no separate Python.
        import contextlib
        import runpy

        class Lines:
            # print() writes a line in pieces; they are put together before logging.
            def __init__(self):
                self.pending = ''

            def write(self, text):
                self.pending += text
                while '\n' in self.pending:
                    line, self.pending = self.pending.split('\n', 1)
                    if line.strip():
                        log(line)

            def flush(self):
                pass
        log('$ ' + script + ' ' + ' '.join(args))
        saved = sys.argv
        sys.argv = [script, *args]
        try:
            with contextlib.redirect_stdout(Lines()), contextlib.redirect_stderr(Lines()):
                runpy.run_path(os.path.join(HERE, 'tools', script), run_name='__main__')
        except SystemExit as e:
            if e.code not in (0, None):
                raise RuntimeError(str(e.code))
        finally:
            sys.argv = saved

    progress(0.0, 'Checking your ROM and recompiling its code')
    tool('recomp.py', '--rom', rom, '--layout', LAYOUT, os.path.join(HERE, 'melonDS', 'src'), GEN)
    tool('apps.py', '--layout', LAYOUT, GEN)
    if android:
        return android_steps(rom, jobs, log, progress, run)
    if xbox:
        return xbox_steps(rom, jobs, log, progress, run, icon)
    progress(0.08, 'Configuring the build')
    forget_moved_build(BUILD)
    prefix = windows_prefix()
    # Reconfigure every time: source updates and dependency changes must reach Ninja.
    run(['cmake', '-S', HERE, '-B', BUILD, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
         *(['-DCMAKE_PREFIX_PATH=' + prefix.replace('\\', '/')] if prefix else []),
         *(['-DCMAKE_OSX_ARCHITECTURES=' + MAC_ARCH, '-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0', '-DSSR_MACOS_SOURCE_DEPS=ON'] if DARWIN else [])])
    progress(0.1, 'Compiling (the first time takes 20-40 minutes)')
    run(['ninja', '-C', BUILD, '-j', str(jobs)], parse_ninja=True)
    if WINDOWS:
        # So that the game also starts outside the MSYS2 window: the libraries it uses go
        # next to it.
        bindir = os.path.dirname(shutil.which('ninja') or shutil.which('g++') or '')
        for dll in ('SDL2.dll', 'libpng16-16.dll', 'zlib1.dll', 'libstdc++-6.dll', 'libgcc_s_seh-1.dll',
                    'libwinpthread-1.dll'):
            if bindir and os.path.exists(os.path.join(bindir, dll)):
                shutil.copy2(os.path.join(bindir, dll), BUILD)
    write_rom_note(rom)
    if os.path.exists(os.path.join(HERE, '.bundle')):
        shutil.copyfile(os.path.join(HERE, '.bundle'), os.path.join(HERE, '.built'))
    progress(1.0, 'Done')


def android_steps(rom, jobs, log, progress, run):
    import tarfile
    ndk = android_ndk(log, progress)
    toolchain = os.path.join(ndk, 'build', 'cmake', 'android.toolchain.cmake').replace('\\', '/')
    android_args = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TOOLCHAIN_FILE=' + toolchain,
                    '-DANDROID_ABI=' + ANDROID_ABI, '-DANDROID_PLATFORM=' + ANDROID_PLATFORM]

    # SDL2, built for Android and installed into a folder of its own.
    sdl_prefix = os.path.join(ANDROID_DIR, 'sdl2-' + ANDROID_ABI)
    sdl_lib = os.path.join(sdl_prefix, 'lib', 'libSDL2.so')
    if not os.path.exists(sdl_lib):
        tar_path = os.path.join(ANDROID_DIR, 'downloads', SDL2_NAME + '.tar.gz')
        download(SDL2_DOWNLOAD[0], SDL2_DOWNLOAD[1], tar_path, log, progress, 'Downloading SDL2', 'sha256')
        with tarfile.open(tar_path) as t:
            t.extractall(ANDROID_DIR)
        progress(0.1, 'Compiling SDL2 for Android')
        sdl_build = os.path.join(ANDROID_DIR, 'build-sdl2-' + ANDROID_ABI)
        run(['cmake', '-S', os.path.join(ANDROID_DIR, SDL2_NAME), '-B', sdl_build, *android_args,
             '-DCMAKE_INSTALL_PREFIX=' + sdl_prefix.replace('\\', '/'), '-DSDL_STATIC=OFF', '-DSDL_TEST=OFF'])
        run(['ninja', '-C', sdl_build, '-j', str(jobs), 'install'])

    # The game, as libmain.so.
    build = os.path.join(HERE, 'build-android-' + ANDROID_ABI)
    progress(0.12, 'Configuring the Android build')
    forget_moved_build(build)
    if not os.path.exists(os.path.join(build, 'build.ninja')):
        prefix = sdl_prefix.replace('\\', '/')
        # Named directly: the NDK's toolchain only searches its own folders.
        run(['cmake', '-S', '.', '-B', build, *android_args,
             '-DSDL2_DIR=' + prefix + '/lib/cmake/SDL2'])
    progress(0.14, 'Compiling for Android (the first time takes 20-40 minutes)')
    run(['ninja', '-C', build, '-j', str(jobs)], parse_ninja=True)

    progress(0.98, 'Packing the APK')
    sys.path.insert(0, os.path.join(HERE, 'tools', 'android'))
    import apk
    apk.build(os.path.join(HERE, 'tools', 'android', 'skeleton.apk'), ANDROID_ABI, os.path.join(build, 'libmain.so'),
              sdl_lib, rom, os.path.join(ANDROID_DIR, 'signing-key.json'), ANDROID_APK)
    write_rom_note(rom)
    log('Made ' + ANDROID_APK)
    progress(1.0, 'Done: ' + ANDROID_APK)


def xbox_steps(rom, jobs, log, progress, run, icon=None):
    import tarfile
    sys.path.insert(0, os.path.join(HERE, 'tools', 'xbox'))
    import xbox
    progress(0.08, 'Looking for Visual Studio')
    vs = xbox.find_visual_studio()
    env = xbox.uwp_env(vs)
    log('Visual Studio: ' + vs['root'])

    # SDL2 for UWP, with Visual Studio's own compiler (C++/CX), installed into a folder of its own.
    sdl_prefix = os.path.join(XBOX_DIR, 'sdl2-uwp')
    sdl_dll = os.path.join(sdl_prefix, 'bin', 'SDL2.dll')
    if not os.path.exists(sdl_dll):
        tar_path = os.path.join(XBOX_DIR, 'downloads', SDL2_NAME + '.tar.gz')
        download(SDL2_DOWNLOAD[0], SDL2_DOWNLOAD[1], tar_path, log, progress, 'Downloading SDL2', 'sha256')
        with tarfile.open(tar_path) as t:
            t.extractall(XBOX_DIR)
        progress(0.1, 'Compiling SDL2 for UWP')
        sdl_build = os.path.join(XBOX_DIR, 'build-sdl2-uwp')
        run(['cmake', '-S', os.path.join(XBOX_DIR, SDL2_NAME), '-B', sdl_build, '-G', vs['generator'], '-A', 'x64',
             '-DCMAKE_GENERATOR_INSTANCE=' + vs['root'].replace('\\', '/'),
             '-DCMAKE_SYSTEM_NAME=WindowsStore', '-DCMAKE_SYSTEM_VERSION=10.0', '-DSDL_TEST=OFF', '-DSDL_STATIC=OFF'],
            env=env)
        run(['cmake', '--build', sdl_build, '--config', 'Release', '--parallel', str(jobs)], env=env)
        run(['cmake', '--install', sdl_build, '--config', 'Release', '--prefix', sdl_prefix], env=env)

    # The game, with clang-cl.
    build = os.path.join(HERE, 'build-xbox')
    progress(0.12, 'Configuring the Xbox build')
    forget_moved_build(build)
    clang = vs['clang_cl'].replace('\\', '/')
    run(['cmake', '-S', HERE, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DSSR_UWP=ON',
         '-DCMAKE_C_COMPILER=' + clang, '-DCMAKE_CXX_COMPILER=' + clang, '-DCMAKE_ASM_COMPILER=' + clang,
         '-DCMAKE_LINKER=' + vs['lld_link'].replace('\\', '/'),
         '-DCMAKE_PREFIX_PATH=' + sdl_prefix.replace('\\', '/')], env=env)
    progress(0.14, 'Compiling for Xbox (the first time takes 20-40 minutes)')
    run(['ninja', '-C', build, '-j', str(jobs)], parse_ninja=True, env=env)

    progress(0.98, 'Packing the MSIX')
    # Without the ROM: the player copies it to the console (see XBOX_HELP).
    msix = xbox.package(os.path.join(build, 'ssrecomp.exe'), sdl_dll,
                        os.path.join(HERE, 'tools', 'appimage', 'ssrecomp-setup.png'), version(),
                        XBOX_OUT, XBOX_DIR, env, run, log, picture=icon or xbox.custom_icon(XBOX_DIR))
    write_rom_note(rom)
    log('Made ' + msix)
    progress(1.0, 'Done: ' + msix)


XBOX_HELP = (
    'Made the Xbox app in ' + XBOX_OUT + ' (SoulSilver-ssrecomp.msix). The ROM is not in '
    'it, but its code is made from your game: do not share it.\n\n'
    'To install it on an Xbox One or Series console in Developer Mode: open the Device Portal '
    '(its address is shown in Dev Home), choose Add, pick SoulSilver-ssrecomp.msix and, when '
    'asked for dependencies, Microsoft.VCLibs.x64.14.00.appx from the same folder. Then, in '
    'Dev Home, select the app, press the View button, choose View details and set its type to Game '
    '(apps get only a part of the processor and memory).\n\n'
    'Then copy your ROM to the console: in the Device Portal open File explorer -> '
    'DevelopmentFiles and upload it there (any name ending in .nds). The app copies it into its '
    'own folder (LocalAppData -> io.github.clickingmaster.ssrecomp_... -> LocalState) the first '
    'time it starts; the save, the settings and game.log are kept there too.\n\n'
    'To install it on this PC instead: double-click SoulSilver-ssrecomp.cer and install it '
    'for the Local Machine into "Trusted People", then open the .msix; start it once and put the '
    'ROM in %LOCALAPPDATA%\\Packages\\io.github.clickingmaster.ssrecomp_...\\LocalState.')


ANDROID_HELP = (
    'Made ' + ANDROID_APK + '\n\n'
    'To install it: copy it to your phone or handheld (USB cable, cloud drive...), open it there '
    'and allow your file manager to install apps when asked. It holds your copy of the game: '
    'do not share it.\n\n'
    'Updates install over it as long as they are built on this computer (with the key in ' +
    os.path.join(ANDROID_DIR, 'signing-key.json') + '). Uninstalling the app deletes its save.')


def play(extra=()):
    rom = read_rom_note()
    # Started from here there is no terminal to see them in (and none at all on Windows):
    # the game's messages go to game.log, made new each time, for bug reports (beside the
    # build log, build.log).
    with open(os.path.join(HERE, 'game.log'), 'w') as log:
        return subprocess.Popen([EXE, rom, '--save', os.path.join(HERE, 'soulsilver.sav'),
                                 '--config', os.path.join(HERE, 'ssrecomp.cfg'), *extra], cwd=HERE,
                                stdout=log, stderr=subprocess.STDOUT, env=child_env())


# ---------------------------------------------------------------------------
# Updates: the packaged setup program replaces itself with the newest release's.

RELEASES = 'https://api.github.com/repos/ClickingMaster/ssrecomp/releases/latest'
UPDATE_ASSETS = {   # (system, machine) -> the release file for it
    ('win32', 'AMD64'): 'ssrecomp-setup-windows-x86_64.exe',
    ('linux', 'x86_64'): 'ssrecomp-setup-linux-x86_64',
    ('linux', 'aarch64'): 'ssrecomp-setup-linux-aarch64.AppImage',
    ('darwin', 'arm64'): 'ssrecomp-setup-macos-arm64.zip',
    ('darwin', 'aarch64'): 'ssrecomp-setup-macos-arm64.zip',
    ('darwin', 'x86_64'): 'ssrecomp-setup-macos-x86_64.zip',
}


def version():
    """This program's version: the newest released version in its CHANGELOG.md."""
    path = os.path.join(sys._MEIPASS if FROZEN else HERE, 'project' if FROZEN else '', 'CHANGELOG.md')
    try:
        with open(path, encoding='utf-8') as f:
            m = re.search(r'^## v(\d+(?:\.\d+)*)', f.read(), re.M)
        return m.group(1) if m else None
    except OSError:
        return None


def version_key(v):
    return tuple(int(x) for x in v.split('.'))


def https_open(url):
    import ssl
    import urllib.request
    context = None
    try:
        import certifi
        context = ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        pass
    req = urllib.request.Request(url, headers={'User-Agent': 'ssrecomp-setup', 'Accept': 'application/vnd.github+json'})
    return urllib.request.urlopen(req, context=context, timeout=20)


def check_update():
    """(version, release page, asset url, sha256 url) of a newer release for this system,
    or None. Raises OSError/ValueError when GitHub cannot be reached."""
    import platform
    current = version()
    asset = UPDATE_ASSETS.get((sys.platform, platform.machine()))
    if not FROZEN or not current or not asset:
        return None
    with https_open(RELEASES) as r:
        rel = json.load(r)
    latest = rel.get('tag_name', '').lstrip('v')
    if not re.fullmatch(r'\d+(\.\d+)*', latest) or version_key(latest) <= version_key(current):
        return None
    files = {a['name']: a['browser_download_url'] for a in rel.get('assets', [])}
    if asset not in files or 'SHA256SUMS.txt' not in files:
        return None
    return latest, rel.get('html_url', ''), files[asset], files['SHA256SUMS.txt']


def install_update(update, progress):
    """Downloads the new setup program next to this one, checks it against the release's
    SHA256SUMS.txt and puts it in this one's place. Returns the path to start."""
    latest, _page, url, sums_url = update
    target = self_path()
    name = url.rsplit('/', 1)[1]
    is_zip = name.endswith('.zip')
    if is_zip:
        if not os.access(os.path.dirname(target), os.W_OK):
            raise RuntimeError('Cannot update the app in this folder. Move it to a folder '
                               'you can write to, then try again.')
        ditto = shutil.which('ditto')
        if not ditto:
            raise RuntimeError('Updating the macOS app requires ditto. '
                               'Restore /usr/bin/ditto or download the update manually.')
    sums = {}
    with https_open(sums_url) as r:
        for line in r.read().decode().splitlines():   # "<sha256>  <file>"
            parts = line.split()
            if len(parts) == 2:
                sums[parts[1].lstrip('*')] = parts[0].lower()
    digest = sums.get(name)
    if not digest:
        raise RuntimeError('The release has no checksum for ' + name + '.')
    download_file = (target + '.download') if is_zip else (target + '.new')
    h = hashlib.sha256()
    with https_open(url) as r, open(download_file, 'wb') as f:
        total, done = int(r.headers.get('Content-Length') or 0), 0
        for block in iter(lambda: r.read(1 << 20), b''):
            f.write(block)
            h.update(block)
            done += len(block)
            progress(done / total if total else None, 'Downloading v%s: %d of %d MB' % (latest, done >> 20, total >> 20))
    if h.hexdigest() != digest:
        os.remove(download_file)
        raise RuntimeError('The download was damaged (checksum mismatch). Try again.')
    if is_zip:
        extract_dir = tempfile.mkdtemp(prefix='ssrecomp-update-', dir=os.path.dirname(target))
        try:
            subprocess.run([ditto, '-x', '-k', download_file, extract_dir], check=True)
            apps = [os.path.join(extract_dir, f) for f in os.listdir(extract_dir) if f.endswith('.app')]
            if not apps:
                raise RuntimeError('No .app bundle found in the downloaded archive.')
            new_app = apps[0]
            old = target + '.old'
            if os.path.exists(old):
                shutil.rmtree(old) if os.path.isdir(old) else os.remove(old)
            had_target = os.path.exists(target)
            if had_target:
                os.replace(target, old)
            try:
                os.replace(new_app, target)
            except OSError:
                if had_target:
                    os.replace(old, target)
                raise
            try:
                shutil.rmtree(old) if os.path.isdir(old) else os.remove(old)
            except OSError:
                pass
        finally:
            if os.path.exists(download_file):
                os.remove(download_file)
            if os.path.exists(extract_dir):
                shutil.rmtree(extract_dir, ignore_errors=True)
        return target
    if WINDOWS:
        # A running .exe cannot be overwritten, but it can be renamed; the old copy is
        # removed the next time the program starts.
        old = target + '.old'
        if os.path.exists(old):
            os.remove(old)
        os.replace(target, old)
    else:
        os.chmod(download_file, 0o755)
    os.replace(download_file, target)
    return target


def restart(path):
    env = child_env()
    env['PYINSTALLER_RESET_ENVIRONMENT'] = '1'   # the new program is a fresh one, not a child of this
    for var in ('APPIMAGE', 'APPDIR', 'OWD', 'ARGV0'):
        env.pop(var, None)
    if DARWIN and (path.endswith('.app') or os.path.isdir(path)):
        if shutil.which('open'):
            subprocess.Popen(['open', '-n', path], env=env, close_fds=True, start_new_session=True)
            return
        exe = os.path.join(path, 'Contents', 'MacOS', 'ssrecomp-setup')
        if os.path.exists(exe):
            path = exe
    subprocess.Popen([path], env=env, close_fds=True, start_new_session=not WINDOWS,
                     creationflags=getattr(subprocess, 'DETACHED_PROCESS', 0) if WINDOWS else 0)


def remove_old_copy():
    if FROZEN:
        old = self_path() + '.old'
        if os.path.exists(old):
            try:
                shutil.rmtree(old) if os.path.isdir(old) else os.remove(old)
            except OSError:
                pass


def built_from_older_source():
    """Packaged: whether the game was built before the last update of the source."""
    try:
        with open(os.path.join(HERE, '.bundle')) as a, open(os.path.join(HERE, '.built')) as b:
            return a.read() != b.read()
    except OSError:
        return False


# ---------------------------------------------------------------------------
# The window.

def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title('ssrecomp setup')
    root.minsize(640, 520)
    events = queue.Queue()

    frame = ttk.Frame(root, padding=16)
    frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='SoulSilver for one screen', font=('TkDefaultFont', 16, 'bold')).pack(anchor='w')
    ttk.Label(frame, wraplength=600, justify='left', text=(
        'Builds the game from your own copy of Pokemon SoulSilver (US). Its code is recompiled on this '
        'computer; nothing of the game is downloaded.' +
        (f' The game is built in {HERE}.' if FROZEN else ''))).pack(anchor='w', pady=(4, 4))

    update_row = ttk.Frame(frame)
    update_row.pack(fill='x', pady=(0, 8))
    update_label = ttk.Label(update_row, text=('Version ' + (version() or '?')) if FROZEN else '')
    update_label.pack(side='left')
    update_btn = ttk.Button(update_row, text='Check for updates')
    if FROZEN:
        update_btn.pack(side='left', padx=8)
    found_update = [None]

    def look_for_update(quiet):
        update_btn.state(['disabled'])

        def worker():
            try:
                events.put(('update', check_update(), quiet))
            except Exception as e:  # no network, GitHub down...
                events.put(('update', e, quiet))
        threading.Thread(target=worker, daemon=True).start()

    def do_update():
        if not found_update[0]:
            look_for_update(False)
            return
        if not messagebox.askyesno('Update', 'Download version %s and restart? Your save and settings are kept; '
                                   'afterwards press Build once to update the game.' % found_update[0][0]):
            return
        update_btn.state(['disabled'])
        build_btn.state(['disabled'])

        def worker():
            try:
                events.put(('updated', install_update(found_update[0], lambda f, l: events.put(('progress', f, l)))))
            except Exception as e:
                events.put(('updated', e))
        threading.Thread(target=worker, daemon=True).start()
    update_btn.config(command=do_update)

    row = ttk.Frame(frame)
    row.pack(fill='x')
    ttk.Label(row, text='Your ROM:').pack(side='left')
    rom_var = tk.StringVar(value=read_rom_note() if os.path.exists(ROM_NOTE) else '')
    ttk.Entry(row, textvariable=rom_var).pack(side='left', fill='x', expand=True, padx=6)

    def browse():
        path = filedialog.askopenfilename(title='Your Pokemon SoulSilver ROM',
                                          filetypes=[('Nintendo DS ROM', '*.nds'), ('All files', '*')])
        if path:
            rom_var.set(path)
            update_checks()
    ttk.Button(row, text='Browse...', command=browse).pack(side='left')

    checks = ttk.Label(frame, wraplength=600, justify='left')
    checks.pack(anchor='w', pady=8)
    commands = []

    def copy_commands():
        root.clipboard_clear()
        root.clipboard_append('\n'.join(commands))
        copy_btn.config(text='Copied - paste into a terminal')
    copy_btn = ttk.Button(frame, text='Copy the commands', command=copy_commands)

    jobs_row = ttk.Frame(frame)
    jobs_row.pack(fill='x')
    ttk.Label(jobs_row, text='Parallel compile jobs:').pack(side='left')
    jobs_var = tk.IntVar(value=default_jobs())
    ttk.Spinbox(jobs_row, from_=1, to=64, width=4, textvariable=jobs_var).pack(side='left', padx=6)
    ttk.Label(jobs_row, text='(fewer if the computer runs out of memory)').pack(side='left')

    bar = ttk.Progressbar(frame, maximum=1.0)
    bar.pack(fill='x', pady=(12, 2))
    status = ttk.Label(frame, text='')
    status.pack(anchor='w')

    log_box = tk.Text(frame, height=10, wrap='none', state='disabled')
    log_box.pack(fill='both', expand=True, pady=6)

    buttons = ttk.Frame(frame)
    buttons.pack(fill='x')
    build_btn = ttk.Button(buttons, text='Build')
    build_btn.pack(side='left')
    play_btn = ttk.Button(buttons, text='Play', command=lambda: play(['--fullscreen']))
    play_btn.pack(side='left', padx=6)
    windowed_btn = ttk.Button(buttons, text='Play in a window', command=lambda: play())
    windowed_btn.pack(side='left')
    android_btn = ttk.Button(buttons, text='Build for Android')
    android_btn.pack(side='right')
    xbox_btn = ttk.Button(buttons, text='Build for Xbox')
    if WINDOWS:
        xbox_btn.pack(side='right', padx=(0, 6))

    def open_folder():
        # The logs (game.log, build.log) are here, for bug reports.
        if WINDOWS:
            os.startfile(HERE)
        else:
            subprocess.Popen(['open' if sys.platform == 'darwin' else 'xdg-open', HERE], env=child_env())
    ttk.Button(buttons, text='Open logs folder', command=open_folder).pack(side='right', padx=6)

    def update_checks():
        lines = []
        rom = rom_var.get().strip()
        if rom:
            problem = check_rom(rom)
            lines.append('ROM: ' + ('Pokemon SoulSilver (US), correct.' if problem is None else problem))
        else:
            lines.append('ROM: choose your Pokemon SoulSilver (US) ROM.')
        missing = missing_tools()
        if missing:
            lines.append('Missing to compile: ' + ', '.join(missing) + '.\nTo install them:\n' + install_hint())
        else:
            lines.append('Build tools: found. SDL2 and libpng will be built automatically.' if DARWIN
                         else 'Compiler and libraries: found.')
        if DARWIN:
            lines.append(ANDROID_MAC_NOTE)
        checks.config(text='\n'.join(lines))
        # The install commands are the indented lines of the hint.
        commands[:] = [l.strip() for l in '\n'.join(lines).splitlines() if l.startswith('  ')]
        if missing and commands:
            copy_btn.config(text='Copy the commands')
            copy_btn.pack(anchor='w', after=checks, pady=(0, 8))
        else:
            copy_btn.pack_forget()
        ready = bool(rom) and os.path.isfile(rom) and not missing
        build_btn.state(['!disabled'] if ready else ['disabled'])
        # Android needs only CMake and Ninja here: its compiler and libraries are downloaded.
        android_ready = not DARWIN and bool(rom) and os.path.isfile(rom) and shutil.which('cmake') and shutil.which('ninja')
        android_btn.state(['!disabled'] if android_ready else ['disabled'])
        # Xbox: Visual Studio brings its own CMake, Ninja and compilers (checked when building).
        xbox_btn.state(['!disabled'] if WINDOWS and bool(rom) and os.path.isfile(rom) else ['disabled'])
        built = os.path.exists(EXE) and os.path.exists(ROM_NOTE)
        for b in (play_btn, windowed_btn):
            b.state(['!disabled'] if built else ['disabled'])
        if built and FROZEN and built_from_older_source():
            lines.append('Updated: press Build once to update the game (your save is kept).')
            checks.config(text='\n'.join(lines))

    def start(android=False, xbox=False):
        build_btn.state(['disabled'])
        android_btn.state(['disabled'])
        xbox_btn.state(['disabled'])
        rom, jobs = rom_var.get().strip(), jobs_var.get()

        def worker():
            try:
                with open(os.path.join(HERE, 'build.log'), 'w', encoding='utf-8', errors='replace') as build_log:
                    def log(text):
                        build_log.write(text + '\n')
                        build_log.flush()
                        events.put(('log', text))
                    try:
                        run_steps(rom, jobs, log, lambda f, l: events.put(('progress', f, l)), android=android, xbox=xbox)
                    except Exception as e:
                        build_log.write('Failed: %s\n' % e)
                        raise
                events.put(('done', None, 'android' if android else 'xbox' if xbox else None))
            except Exception as e:  # shown to the user
                events.put(('done', str(e), None))
        threading.Thread(target=worker, daemon=True).start()
    build_btn.config(command=start)
    android_btn.config(command=lambda: start(android=True))
    xbox_btn.config(command=lambda: start(xbox=True))

    def pump():
        try:
            while True:
                ev = events.get_nowait()
                if ev[0] == 'log':
                    log_box.config(state='normal')
                    log_box.insert('end', ev[1] + '\n')
                    log_box.see('end')
                    log_box.config(state='disabled')
                elif ev[0] == 'progress':
                    if ev[1] is not None:
                        bar['value'] = ev[1]
                    status.config(text=ev[2])
                elif ev[0] == 'update':
                    update_btn.state(['!disabled'])
                    result, quiet = ev[1], ev[2]
                    if isinstance(result, Exception):
                        if not quiet:
                            messagebox.showerror('Update', 'Could not check for updates: %s' % result)
                    elif result:
                        found_update[0] = result
                        update_label.config(text='Version %s - version %s is available.' % (version(), result[0]))
                        update_btn.config(text='Update to %s' % result[0])
                    elif not quiet:
                        messagebox.showinfo('Update', 'This is the newest version.')
                elif ev[0] == 'updated':
                    if isinstance(ev[1], Exception):
                        update_btn.state(['!disabled'])
                        update_checks()
                        status.config(text='Update failed')
                        messagebox.showerror('Update failed', '%s\n\nYou can also download it from %s' %
                                             (ev[1], found_update[0][1]))
                    else:
                        restart(ev[1])
                        root.destroy()
                        return
                elif ev[0] == 'done':
                    update_checks()
                    if ev[1]:
                        status.config(text='Failed')
                        messagebox.showerror('Build failed', ev[1])
                    elif ev[2] == 'android':
                        messagebox.showinfo('Android build ready', ANDROID_HELP)
                    elif ev[2] == 'xbox':
                        messagebox.showinfo('Xbox build ready', XBOX_HELP)
                    else:
                        messagebox.showinfo('Ready', 'The game is built. Press Play to start it.')
        except queue.Empty:
            pass
        root.after(100, pump)

    update_checks()
    if FROZEN:
        look_for_update(True)
    pump()
    root.mainloop()


def main():
    remove_old_copy()
    unpack()
    find_msys2()
    find_macos_tools()
    args = sys.argv[1:]
    if DARWIN and '--android' in args:
        sys.exit(ANDROID_MAC_NOTE)
    if not WINDOWS and '--xbox' in args:
        sys.exit(XBOX_WINDOWS_NOTE)
    if '--update' in args:
        # Without the window: replace this program with the newest release's.
        if not FROZEN:
            sys.exit('Running from the source: update it with git pull instead.')
        try:
            update = check_update()
            if not update:
                print('Version %s is the newest.' % version())
                return
            print('Updating from %s to %s' % (version(), update[0]))
            out = sys.__stdout__
            path = install_update(update, lambda f, l: print(l, file=out, flush=True))
        except Exception as e:
            sys.exit('Update failed: %s' % e)
        print('Updated: %s. Run it again with --rom to rebuild the game.' % path)
        return
    if '--rom' in args:
        rom = args[args.index('--rom') + 1]
        jobs = int(args[args.index('--jobs') + 1]) if '--jobs' in args else default_jobs()
        problem = check_rom(rom)
        if problem:
            print(problem)
        android = '--android' in args
        xbox = '--xbox' in args
        missing = ([] if xbox else [t for t in ('cmake', 'ninja') if not shutil.which(t)] if android
                   else missing_tools())
        if missing:
            sys.exit('Missing: ' + ', '.join(missing) + '\n' + install_hint())
        try:
            # Straight to the terminal: while the recompiler runs, print() itself is
            # redirected into this log.
            out = sys.__stdout__
            last = [0.0]

            def progress(f, label):
                if f is not None:
                    last[0] = f
                print(f'[{int(last[0] * 100):3d}%] {label}', file=out, flush=True)
            icon = args[args.index('--xbox-icon') + 1] if '--xbox-icon' in args else None
            run_steps(rom, jobs, lambda t: print(t, file=out, flush=True), progress, android=android, xbox=xbox,
                      icon=icon)
        except RuntimeError as e:
            sys.exit(str(e))
        if android:
            print(ANDROID_HELP)
            return
        if xbox:
            print(XBOX_HELP)
            return
        print('Built. Start the game by running this program again (without --rom) and pressing Play' +
              ('.' if FROZEN else ', or with ./run.sh --fullscreen.'))
        return
    try:
        import tkinter  # noqa: F401
    except ImportError:
        if DARWIN:
            sys.exit('This needs Python with Tk support.\n' + install_hint())
        sys.exit('This needs Python\'s Tk support for its window. ' + (
            'Install it (python3-tk on Ubuntu/Debian, python3-tkinter on Fedora, tk on Arch) '
            'or run: python3 ssrecomp_setup.py --rom <your ROM>'))
    gui()


if __name__ == '__main__':
    main()
