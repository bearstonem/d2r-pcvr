"""Packaging fixtures: reject unverified files before writing a payload."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import stage_native_installer as staging


class StagingTests(unittest.TestCase):
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
                     '../outside.txt', 'bin/C:escape.dll', 'bin\\escape.dll'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(ValueError):
                    self.stage(root, {name: b'forbidden fixture'})
                self.assertFalse((root / 'staged').exists())

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


if __name__ == '__main__':
    unittest.main()
