"""ROM-free portability regressions. Run: python3 -m unittest discover -s tests"""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import recomp
import ssrecomp_setup as setup


class DarwinSetupTests(unittest.TestCase):
    def test_memory_budget(self):
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
             patch.object(os, 'cpu_count', return_value=16), \
             patch.object(subprocess, 'check_output', return_value=str(16 * 1024**3)):
            self.assertEqual(setup.default_jobs(), 4)
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
             patch.object(os, 'cpu_count', return_value=16), \
             patch.object(subprocess, 'check_output', side_effect=OSError):
            self.assertEqual(setup.default_jobs(), 2)

    def test_finder_tool_search(self):
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'MAC_ARCH', 'arm64'), \
             patch.dict(os.environ, {'PATH': '/usr/bin:/bin'}, clear=True):
            setup.find_macos_tools()
            paths = os.environ['PATH'].split(os.pathsep)
            self.assertIn('/opt/homebrew/bin', paths)
            self.assertIn('/usr/local/bin', paths)
            self.assertEqual(os.environ['CXX'], 'clang++')

    def test_android_rejected_before_rom_or_download(self):
        with patch.object(setup, 'DARWIN', True):
            with self.assertRaisesRegex(RuntimeError, 'not supported'):
                setup.run_steps('no ROM supplied', 2, print, print, android=True)
            with self.assertRaisesRegex(RuntimeError, 'not supported'):
                setup.android_ndk(print, print)

    def test_compiler_architecture_error_is_actionable(self):
        setup.macos_toolchain_problem.cache_clear()
        result = subprocess.CompletedProcess([], 1, 'incompatible architecture: x86_64, expected arm64')
        with patch.object(setup, 'MAC_ARCH', 'arm64'), \
             patch.object(subprocess, 'run', return_value=result):
            self.assertIn('incompatible architecture', setup.macos_toolchain_problem('test', ''))
        setup.macos_toolchain_problem.cache_clear()

    def test_rebuild_reconfigures_and_honors_explicit_jobs(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp build with spaces ') as tmp:
            build = str(Path(tmp) / 'build-macos-arm64')
            Path(build).mkdir()
            (Path(build) / 'CMakeCache.txt').write_text('CMAKE_CACHEFILE_DIR:INTERNAL=/old/location\n')
            (Path(build) / 'CMakeFiles').mkdir()
            (Path(build) / 'ssrecomp').write_text('keep executable')
            proc = unittest.mock.Mock()
            proc.stdout = iter(())
            proc.wait.return_value = 0
            with patch.object(setup, 'HERE', tmp), patch.object(setup, 'BUILD', build), \
                 patch.object(setup, 'ROM_NOTE', str(Path(tmp) / 'rom path')), \
                 patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
                 patch.object(setup, 'MAC_ARCH', 'arm64'), \
                 patch('runpy.run_path'), patch.object(subprocess, 'Popen', return_value=proc) as popen, \
                 patch.object(subprocess, 'check_output', return_value='/opt/homebrew'):
                for _ in range(2):
                    setup.run_steps('/synthetic ROM.nds', 3, lambda t: None, lambda f, l: None)
            self.assertFalse((Path(build) / 'CMakeCache.txt').exists())
            self.assertFalse((Path(build) / 'CMakeFiles').exists())
            self.assertEqual((Path(build) / 'ssrecomp').read_text(), 'keep executable')
            commands = [c.args[0] for c in popen.call_args_list]
            configs = [c for c in commands if c[0] == 'cmake']
            self.assertEqual(len(configs), 2)
            self.assertIn('-DCMAKE_OSX_ARCHITECTURES=arm64', configs[0])
            self.assertIn(build, configs[0])
            self.assertEqual(commands[-1], ['ninja', '-C', build, '-j', '3'])

    def test_portable_launch_resolves_rom_and_replaces_log(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp portable ') as tmp:
            note = Path(tmp) / 'ssrecomp.rom'
            note.write_text('roms/game.nds\n')
            log = Path(tmp) / 'game.log'
            log.write_text('old launch\n')
            with patch.object(setup, 'HERE', tmp), patch.object(setup, 'ROM_NOTE', str(note)):
                for launch in ('first', 'second'):
                    def spawn(cmd, **kwargs):
                        self.assertEqual(cmd[1], os.path.join(tmp, 'roms', 'game.nds'))
                        self.assertIn('--config', cmd)
                        kwargs['stdout'].write(launch + '\n')
                    with patch.object(subprocess, 'Popen', side_effect=spawn):
                        setup.play()
                    self.assertEqual(log.read_text(), launch + '\n')

    def test_packaged_updates_preserve_user_files(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp update with spaces ') as tmp:
            data, payload = Path(tmp) / 'user data', Path(tmp) / 'payload'
            (payload / 'project/runtime').mkdir(parents=True)
            (payload / 'project/runtime/version.txt').write_text('new source')
            (payload / 'bundle.id').write_text('new version')
            data.mkdir()
            for name in ('soulsilver.sav', 'ssrecomp.cfg', 'ssrecomp.rom'):
                (data / name).write_text('keep me')
            with patch.object(setup, 'FROZEN', True), patch.object(setup, 'HERE', str(data)), \
                 patch.object(sys, '_MEIPASS', str(payload), create=True):
                setup.unpack()
                setup.unpack()
            self.assertEqual((data / 'runtime/version.txt').read_text(), 'new source')
            for name in ('soulsilver.sav', 'ssrecomp.cfg', 'ssrecomp.rom'):
                self.assertEqual((data / name).read_text(), 'keep me')

    def test_self_bundle_path_and_self_path(self):
        expected = os.path.abspath('/Applications/ssrecomp-setup.app')
        bundle_exe = os.path.join(expected, 'Contents', 'MacOS', 'ssrecomp-setup')
        with patch.object(setup, 'DARWIN', True), patch.object(sys, 'executable', bundle_exe):
            self.assertEqual(setup.self_bundle_path(), expected)
            self.assertEqual(setup.self_path(), expected)
        plain_exe = os.path.abspath('/usr/local/bin/ssrecomp-setup')
        with patch.object(setup, 'DARWIN', True), patch.object(sys, 'executable', plain_exe):
            self.assertIsNone(setup.self_bundle_path())
            self.assertEqual(setup.self_path(), plain_exe)

    def test_macos_check_update_finds_assets(self):
        fake_release = {
            'tag_name': 'v0.5.0',
            'html_url': 'https://github.com/ClickingMaster/ssrecomp/releases/tag/v0.5.0',
            'assets': [
                {'name': 'ssrecomp-setup-macos-arm64.zip', 'browser_download_url': 'https://dl/arm64.zip'},
                {'name': 'ssrecomp-setup-macos-x86_64.zip', 'browser_download_url': 'https://dl/x86_64.zip'},
                {'name': 'SHA256SUMS.txt', 'browser_download_url': 'https://dl/SHA256SUMS.txt'},
            ]
        }
        for arch, expected_asset in (('arm64', 'https://dl/arm64.zip'), ('x86_64', 'https://dl/x86_64.zip')):
            with patch.object(setup, 'DARWIN', True), patch.object(sys, 'platform', 'darwin'), \
                 patch.object(platform, 'machine', return_value=arch), \
                 patch.object(setup, 'FROZEN', True), patch.object(setup, 'version', return_value='0.4.0'), \
                 patch.object(setup, 'https_open') as mock_open:
                mock_resp = unittest.mock.MagicMock()
                mock_resp.read.return_value = json.dumps(fake_release).encode()
                mock_resp.__enter__.return_value = mock_resp
                mock_open.return_value = mock_resp
                res = setup.check_update()
                self.assertIsNotNone(res)
                self.assertEqual(res[0], '0.5.0')
                self.assertEqual(res[2], expected_asset)

    def test_macos_install_update_replaces_app_bundle(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp mac update ') as tmp:
            app = Path(tmp) / 'ssrecomp-setup.app'
            (app / 'Contents/MacOS').mkdir(parents=True)
            (app / 'Contents/MacOS/ssrecomp-setup').write_text('#!/bin/sh\necho v1\n')
            (app / 'Contents/MacOS/ssrecomp-setup').chmod(0o755)

            new_bundle = Path(tmp) / 'new_src/ssrecomp-setup.app'
            (new_bundle / 'Contents/MacOS').mkdir(parents=True)
            (new_bundle / 'Contents/MacOS/ssrecomp-setup').write_text('#!/bin/sh\necho v2\n')
            (new_bundle / 'Contents/MacOS/ssrecomp-setup').chmod(0o755)

            zip_path = Path(tmp) / 'release.zip'
            import zipfile
            with zipfile.ZipFile(zip_path, 'w') as z:
                for base, _, files in os.walk(new_bundle):
                    for f in files:
                        full = Path(base) / f
                        rel = full.relative_to(new_bundle.parent)
                        zinfo = zipfile.ZipInfo.from_file(full, str(rel))
                        zinfo.external_attr = 0o100755 << 16
                        z.writestr(zinfo, full.read_bytes())

            zip_bytes = zip_path.read_bytes()
            zip_sha256 = hashlib.sha256(zip_bytes).hexdigest()
            sums_text = f'{zip_sha256}  ssrecomp-setup-macos-arm64.zip\n'

            def fake_open(url):
                mock_resp = unittest.mock.MagicMock()
                if url.endswith('SHA256SUMS.txt'):
                    mock_resp.read.return_value = sums_text.encode()
                else:
                    mock_resp.headers = {'Content-Length': str(len(zip_bytes))}
                    mock_resp.read.side_effect = [zip_bytes, b'']
                mock_resp.__enter__.return_value = mock_resp
                return mock_resp

            update_info = ('0.5.0', 'https://page', 'https://dl/ssrecomp-setup-macos-arm64.zip', 'https://dl/SHA256SUMS.txt')
            with patch.object(setup, 'DARWIN', True), patch.object(setup, 'self_path', return_value=str(app)), \
                 patch.object(setup, 'https_open', side_effect=fake_open):
                result = setup.install_update(update_info, lambda f, l: None)
            self.assertEqual(result, str(app))
            self.assertEqual((app / 'Contents/MacOS/ssrecomp-setup').read_text(), '#!/bin/sh\necho v2\n')
            self.assertFalse((Path(tmp) / 'ssrecomp-setup.app.download').exists())
            self.assertFalse((Path(tmp) / 'ssrecomp-setup.app.old').exists())

    def test_macos_restart_app_bundle(self):
        with patch.object(setup, 'DARWIN', True), patch.object(setup, 'WINDOWS', False), \
             patch('shutil.which', return_value='/usr/bin/open'), \
             patch('subprocess.Popen') as popen:
            setup.restart('/Applications/ssrecomp-setup.app')
            popen.assert_called_once()
            args = popen.call_args[0][0]
            self.assertEqual(args, ['open', '-n', '/Applications/ssrecomp-setup.app'])

    def test_remove_old_copy_cleans_bundle(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp old bundle ') as tmp:
            app = Path(tmp) / 'ssrecomp-setup.app'
            old = Path(tmp) / 'ssrecomp-setup.app.old'
            old.mkdir()
            (old / 'Contents').mkdir()
            (old / 'Contents/info.txt').write_text('old')
            with patch.object(setup, 'FROZEN', True), patch.object(setup, 'self_path', return_value=str(app)):
                setup.remove_old_copy()
            self.assertFalse(old.exists())

    def test_windows_and_linux_update_compatibility(self):
        # Verify self_path and restart on Windows
        with patch.object(setup, 'DARWIN', False), patch.object(setup, 'WINDOWS', True), \
             patch.object(sys, 'executable', r'C:\ssrecomp-setup.exe'), \
             patch.dict(os.environ, {}, clear=True):
            self.assertEqual(setup.self_path(), r'C:\ssrecomp-setup.exe')
            with patch('subprocess.Popen') as popen:
                setup.restart(setup.self_path())
                self.assertEqual(popen.call_args[0][0], [r'C:\ssrecomp-setup.exe'])

        # Verify self_path and restart on Linux (AppImage)
        with patch.object(setup, 'DARWIN', False), patch.object(setup, 'WINDOWS', False), \
             patch.object(sys, 'executable', '/tmp/mount_123/usr/bin/ssrecomp-setup'), \
             patch.dict(os.environ, {'APPIMAGE': '/home/user/ssrecomp-setup-linux-aarch64.AppImage'}):
            self.assertEqual(setup.self_path(), '/home/user/ssrecomp-setup-linux-aarch64.AppImage')
            with patch('subprocess.Popen') as popen:
                setup.restart(setup.self_path())
                self.assertEqual(popen.call_args[0][0], ['/home/user/ssrecomp-setup-linux-aarch64.AppImage'])


class AssemblyFormatTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('clang') and sys.platform != 'win32', 'cross assembler needs clang')
    def test_macho_elf_and_coff_assemble_same_generated_file(self):
        with tempfile.TemporaryDirectory(prefix='ssrecomp object formats ') as tmp:
            dest = Path(tmp)
            sections = [recomp.Section('one', 0, b'\x01\x02\xff'), recomp.Section('two', 16, b'\x04\x05')]
            recomp.write_images(tmp, sections)
            for target in ('arm64-apple-macos11', 'x86_64-apple-macos11',
                           'x86_64-unknown-linux-gnu', 'x86_64-w64-windows-gnu'):
                subprocess.run(['clang', '-target', target, '-c', str(dest / 'rc_images.S'),
                                '-Wa,-I' + tmp, '-o', str(dest / (target + '.o'))], check=True,
                               capture_output=True)


if __name__ == '__main__':
    unittest.main()
