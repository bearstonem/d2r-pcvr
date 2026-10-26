"""Packaging fixtures: reject unverified files before writing a payload."""
import ast
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import stage_native_installer as staging


class StagingTests(unittest.TestCase):
    def test_coop_assistant_helpers_are_allowlisted_and_hashed(self):
        source=Path(__file__).resolve().parent.parent
        names=('Coop-Setup.cmd','docs/MULTIPLAYER.md','steamvr/coop_assistant.py',
               'steamvr/coop_setup.py','steamvr/coop_connection.py','steamvr/coop_wizard.py')
        files={name:(source/name).read_bytes() for name in names}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);manifest=self.stage(root,files)
            for name,data in files.items():
                self.assertEqual((root/'staged'/name).read_bytes(),data)
                self.assertEqual(manifest['sha256'][name],hashlib.sha256(data).hexdigest())

    def fixture(self, root, extra=None, changed=False):
        files = {'steamvr/setup_native.py': b'backend', 'steamvr/mod_identity.py': b'identity', 'bin/d2rl-pcvr.dll': b'plugin',
                 'bin/openxr_loader.dll': b'loader', **(extra or {})}
        manifest = {'platform': 'native-windows', 'source_commit': 'source',
                    'release_tag': 'v0.3.17-windows-preview',
                    'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}
        package, runtime = root / 'mod.zip', root / 'python.zip'
        with zipfile.ZipFile(package, 'w') as archive:
            for name, data in files.items():
                archive.writestr('d2r-pcvr-preview/' + name, b'altered' if changed else data)
            archive.writestr('d2r-pcvr-preview/release-manifest.json', json.dumps(manifest))
        with zipfile.ZipFile(runtime, 'w') as archive:
            archive.writestr('python.exe', b'official runtime fixture')
            archive.writestr('LICENSE.txt', b'Python license fixture')
            archive.writestr('python312._pth', b'old import paths')
        return package, runtime, root / 'staged'

    def stage(self, root, extra=None, changed=False):
        package, runtime, destination = self.fixture(root, extra, changed)
        with patch.object(staging, 'PYTHON_SHA256', hashlib.sha256(runtime.read_bytes()).hexdigest()):
            return staging.stage(package, runtime, destination, root)

    def test_includes_private_runtime_license_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.stage(root)
            self.assertEqual(manifest['distribution'], 'graphical-windows-installer')
            self.assertIn('.runtime/python/LICENSE.txt', manifest['sha256'])
            self.assertNotIn('.runtime/python/python312._pth', manifest['sha256'])
            self.assertIn('../../steamvr', (root / 'staged/.runtime/python/python312._pth').read_text())
            for name, digest in manifest['sha256'].items():
                self.assertEqual(hashlib.sha256((root / 'staged' / name).read_bytes()).hexdigest(), digest)

    def test_stages_launcher_manifest_and_its_icon(self):
        source = Path(__file__).resolve().parent.parent
        descriptor = json.loads((source / 'vrmod-manifest.json').read_text(encoding='utf-8'))
        files = {name: (source / name).read_bytes()
                 for name in ('vrmod-manifest.json', descriptor['icon'])}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.stage(root, files)
            for name, data in files.items():
                self.assertEqual((root / 'staged' / name).read_bytes(), data)
                self.assertEqual(manifest['sha256'][name], hashlib.sha256(data).hexdigest())

    def test_stages_opt_in_developer_helpers_without_generated_content(self):
        helpers = {'Launch-Dev-SteamVR.cmd': b'launch fixture',
                   'steamvr/DEVELOPMENT_SCENARIOS.md': b'instructions fixture',
                   'steamvr/prepare_dev_scenarios.py': b'prepare fixture'}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.stage(root, helpers)
            for name, data in helpers.items():
                self.assertEqual((root / 'staged' / name).read_bytes(), data)
                self.assertEqual(manifest['sha256'][name], hashlib.sha256(data).hexdigest())
        for name in ('mods/pcvr-dev/pcvr-dev.mpq/data/global/excel/levels.txt',
                     'steamvr/prepare_private_experiment.py', 'Launch-Private-Experiment.cmd'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaisesRegex(ValueError, 'allowlist'):
                    self.stage(root, {**helpers, name: b'not distributable'})
                self.assertFalse((root / 'staged').exists())

    def test_rejects_game_assets_saves_logs_and_path_traversal(self):
        for name in ('D2R.exe', 'Character.d2s', 'game.log', 'steamvr/d2r_vr_icons/cache.json',
                     '../outside.txt', 'bin/C:escape.dll', 'bin\\escape.dll',
                     'mods/D2R-PCVR/D2R-PCVR.mpq/data/hd/env/preset/act3/kurast/burbssewer0.json'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(ValueError):
                    self.stage(root, {name: b'forbidden fixture'})
                self.assertFalse((root / 'staged').exists())

    def test_stages_sewer_preparation_code_only(self):
        name = 'steamvr/prepare_sewer_backdrop.py'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = self.stage(root, {name: b'local archive preparation fixture'})
            self.assertIn(name, manifest['sha256'])
            self.assertEqual((root/'staged'/name).read_bytes(), b'local archive preparation fixture')

    def test_every_module_setup_imports_is_packaged_and_staged(self):
        # install.py imported prepare_props at its top without the package or
        # the staging allowlist listing it: Setup would have stopped at once
        # (found while cutting v0.3.25). Follow Setup's local imports.
        repository = Path(__file__).resolve().parent.parent
        steamvr = repository / 'steamvr'
        package = (steamvr / 'package.py').read_text(encoding='utf-8')
        entry = ['install.py', 'setup_native.py', 'setup_windows.py', 'launch_windows.py', 'uninstall_windows.py']
        seen = set()
        while entry:
            name = entry.pop()
            if name in seen:
                continue
            seen.add(name)
            for node in ast.walk(ast.parse((steamvr / name).read_text(encoding='utf-8'))):
                modules = ([alias.name for alias in node.names] if isinstance(node, ast.Import) else
                           [node.module] if isinstance(node, ast.ImportFrom) and node.module and not node.level else [])
                for module in modules:
                    local = module.split('.')[0] + '.py'
                    if (steamvr / local).is_file():
                        entry.append(local)
        self.assertIn('prepare_props.py', seen)
        for name in sorted(seen):
            with self.subTest(module=name):
                self.assertIn(name, staging.SCRIPTS)
                self.assertIn(f"'steamvr/{name}'", package)

    def test_rejects_changed_manifest_payload_and_unmanifested_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                self.stage(root, changed=True)
            self.assertFalse((root / 'staged').exists())
            package, runtime, destination = self.fixture(root)
            with zipfile.ZipFile(package, 'a') as archive:
                archive.writestr('d2r-pcvr-preview/untracked.txt', 'untracked fixture')
            with patch.object(staging, 'PYTHON_SHA256', hashlib.sha256(runtime.read_bytes()).hexdigest()):
                with self.assertRaisesRegex(ValueError, 'not covered'):
                    staging.stage(package, runtime, destination, root)
            self.assertFalse(destination.exists())

    def test_rejects_wrong_python_checksum_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package, runtime, destination = self.fixture(root)
            with self.assertRaisesRegex(ValueError, 'Python archive checksum'):
                staging.stage(package, runtime, destination, root)
            destination.mkdir()
            marker = destination / 'keep.txt'
            marker.write_bytes(b'preserve')
            with patch.object(staging, 'PYTHON_SHA256', hashlib.sha256(runtime.read_bytes()).hexdigest()):
                with self.assertRaisesRegex(ValueError, 'new staging directory'):
                    staging.stage(package, runtime, destination, root)
            self.assertEqual(marker.read_bytes(), b'preserve')

    def test_installer_version_must_match_package_before_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package, runtime, destination = self.fixture(root)
            with patch.object(staging, 'PYTHON_SHA256', hashlib.sha256(runtime.read_bytes()).hexdigest()):
                with self.assertRaisesRegex(ValueError, 'Installer version'):
                    staging.stage(package, runtime, destination, root, '0.3.18')
                self.assertFalse(destination.exists())
                manifest = staging.stage(package, runtime, destination, root, '0.3.17')
                self.assertEqual(manifest['release_tag'], 'v0.3.17-windows-preview')

    def test_carries_only_the_pinned_launcher_setup_unmodified(self):
        name, data = 'D2R_VR_Launcher_Setup_0.2.0.exe', b'launcher setup fixture'
        digest = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package, runtime, destination = self.fixture(root)
            launcher = root / name
            launcher.write_bytes(data)
            with patch.object(staging, 'PYTHON_SHA256', hashlib.sha256(runtime.read_bytes()).hexdigest()):
                for path, pin, message in ((launcher, '0' * 64, 'Launcher Setup checksum'), (launcher, None, 'published name'),
                                           (root / 'Setup.exe', digest, 'published name')):
                    (root / 'Setup.exe').write_bytes(data)
                    with self.subTest(pin=pin, path=path.name), self.assertRaisesRegex(ValueError, message):
                        staging.stage(package, runtime, destination, root, None, path, pin)
                    self.assertFalse(destination.exists())
                manifest = staging.stage(package, runtime, destination, root, None, launcher, digest)
            self.assertEqual((destination / 'launcher' / name).read_bytes(), data)
            self.assertEqual(manifest['sha256']['launcher/' + name], digest)
            self.assertEqual(manifest['carried_launcher'], {'file': 'launcher/' + name, 'version': '0.2.0', 'sha256': digest})
            self.assertEqual(json.loads((destination / 'release-manifest.json').read_text())['sha256']['launcher/' + name], digest)


if __name__ == '__main__':
    unittest.main()
