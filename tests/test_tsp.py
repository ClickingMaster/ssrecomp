"""Exercise SD deployment with synthetic files, without a ROM or device."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TspDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ssrecomp TSP with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.port = self.root / 'port'
        self.port.mkdir()
        for name in ('deploy_to_sd.sh', 'Pokemon SoulSilver.sh',
                     'Pokemon SoulSilver ES.sh', 'diag.sh'):
            shutil.copy2(ROOT / 'port' / name, self.port / name)
        shutil.copytree(ROOT / 'port/soulsilver', self.port / 'soulsilver')
        self.card = self.root / 'SD card'
        self.ports = self.card / 'Roms/PORTS'
        self.ports.mkdir(parents=True)
        self.data = self.card / 'Data/ports/soulsilver'
        self.bin = self.root / 'synthetic executable'
        self.bin.write_text('#!/bin/sh\nexit 0\n')
        self.bin.chmod(0o755)
        tools = self.root / 'tools'
        tools.mkdir()
        (tools / 'sync').write_text('#!/bin/sh\nexit 0\n')
        (tools / 'sync').chmod(0o755)
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('SSR_')}
        self.env.update(SSR_SD_MOUNT=str(self.card), SSR_BIN=str(self.bin),
                        PATH=str(tools) + os.pathsep + os.environ['PATH'])

    def deploy(self, **env):
        result = subprocess.run(['bash', str(self.port / 'deploy_to_sd.sh')],
                                env=dict(self.env, **env), text=True,
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def supply_roms(self):
        # These are text placeholders, not executable game content.
        english = self.root / 'English placeholder.nds'
        spanish = self.root / 'Spanish placeholder.nds'
        english.write_text('synthetic English input')
        spanish.write_text('synthetic Spanish input')
        return english, spanish

    def test_both_entries_preserve_saves_settings_and_existing_roms(self):
        english, spanish = self.supply_roms()
        self.deploy(SSR_ROM=str(english), SSR_ROM_ES=str(spanish))
        for name in ('Pokemon SoulSilver.sh', 'Pokemon SoulSilver ES.sh'):
            self.assertTrue(os.access(self.ports / name, os.X_OK))
        self.assertTrue(os.access(self.data / 'diag.sh', os.X_OK))
        self.assertEqual((self.data / 'soulsilver.nds').read_text(), english.read_text())
        self.assertEqual((self.data / 'soulsilver_es.nds').read_text(), spanish.read_text())
        keep = {
            'ssrecomp.cfg': 'player settings',
            'soulsilver.sav': 'English progress',
            'soulsilver_es.sav': 'Spanish progress',
            'soulsilver.sav.state': 'English snapshot placeholder',
            'soulsilver_es.sav.state': 'Spanish snapshot placeholder',
        }
        for name, text in keep.items():
            (self.data / name).write_text(text)
        english.write_text('replacement English input')
        spanish.write_text('replacement Spanish input')
        self.deploy(SSR_ROM=str(english), SSR_ROM_ES=str(spanish))
        for name, text in keep.items():
            self.assertEqual((self.data / name).read_text(), text)
        self.assertEqual((self.data / 'soulsilver.nds').read_text(), 'synthetic English input')
        self.assertEqual((self.data / 'soulsilver_es.nds').read_text(), 'synthetic Spanish input')
        self.deploy(SSR_ROM=str(english), SSR_ROM_ES=str(spanish), FORCE_ROM='1')
        self.assertEqual((self.data / 'soulsilver.nds').read_text(), english.read_text())
        self.assertEqual((self.data / 'soulsilver_es.nds').read_text(), spanish.read_text())
        for name, text in keep.items():
            self.assertEqual((self.data / name).read_text(), text)

    def test_deploy_without_roms_and_add_launcher_for_existing_spanish_rom(self):
        self.deploy()
        self.assertTrue((self.ports / 'Pokemon SoulSilver.sh').exists())
        self.assertFalse((self.ports / 'Pokemon SoulSilver ES.sh').exists())
        (self.data / 'soulsilver_es.nds').write_text('already supplied placeholder')
        self.deploy()
        self.assertTrue((self.ports / 'Pokemon SoulSilver ES.sh').exists())

    def test_spanish_wrapper_selects_separate_save_and_forwards_exit_status(self):
        # Replace only the common launcher in the temporary fixture: do not
        # change CPU clocks or stop services on the machine running the tests.
        (self.port / 'Pokemon SoulSilver.sh').write_text(
            '#!/bin/bash\nprintf "%s\\n%s\\n" "$SSR_ROM_NAME" "$SSR_SAVE_NAME"\nexit 7\n')
        result = subprocess.run(['bash', str(self.port / 'Pokemon SoulSilver ES.sh')],
                                env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout, 'soulsilver_es.nds\nsoulsilver_es.sav\n')


if __name__ == '__main__':
    unittest.main()
