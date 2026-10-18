import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package
import release_notes as notes

ROOT = Path(__file__).resolve().parent.parent


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.document = self.root / 'RELEASE_NOTES.md'
        self.identity = self.root / 'identity.h'
        self.document.write_text('''# D2R VR Unleashed v0.3.17 - Windows preview

Longer public notes here.
<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.17-windows-preview
credits: Community contributors; BodyWalkVR.
- First feature.
- Second feature.
PCVR_IN_HEADSET_NOTES -->
''', encoding='utf-8')
        self.identity.write_text('#define D2RPCVR_VERSION "0.3.17"\n', encoding='utf-8')

    def read(self):
        return notes.read_notes(self.document, self.identity)

    def change(self, before, after):
        self.document.write_text(self.document.read_text(encoding='utf-8').replace(before, after), encoding='utf-8')

    def test_generates_version_author_links_and_current_summary(self):
        data = self.read()
        self.assertEqual(data['author'], 'A mod by monkeylashes')
        self.assertEqual(data['release_url'], notes.PROJECT_URL + '/releases/tag/v0.3.17-windows-preview')
        self.assertEqual(data['lines'], ['First feature.', 'Second feature.'])
        self.assertIn('v0.3.17', data['title'])
        self.assertIn(notes.marker(data), notes.header(data))

    def test_version_bump_cannot_keep_old_summary_tag(self):
        self.identity.write_text('#define D2RPCVR_VERSION "0.3.18"\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'heading must match'):
            self.read()
        self.change('v0.3.17 - Windows', 'v0.3.18 - Windows')
        with self.assertRaisesRegex(ValueError, 'summary release tag'):
            self.read()
        self.change('release: v0.3.17', 'release: v0.3.18')
        self.change('First feature.', 'New release feature.')
        data = self.read()
        self.assertEqual(data['version'], '0.3.18')
        self.assertIn('v0.3.18-windows-preview', data['release_url'])
        self.assertIn('New release feature.', notes.header(data))

    def test_missing_duplicate_and_overlong_blocks_refused(self):
        original = self.document.read_text(encoding='utf-8')
        for invalid in (original.replace(notes.START, ''), original + original,
                        original.replace('First feature.', 'W' * 101),
                        original.replace('- Second feature.', '\n'.join('- Feature.' for _ in range(14)))):
            self.document.write_text(invalid, encoding='utf-8')
            with self.assertRaises(ValueError):
                self.read()

    def test_native_header_escapes_text_and_preserves_credits(self):
        self.change('First feature.', 'A "quoted" feature in C:\\folder.')
        data = self.read()
        self.assertIn(json.dumps(data['lines'][0]), notes.header(data))
        self.assertIn('BodyWalkVR', notes.header(data))

    def test_checked_in_template_can_be_used_without_stale_placeholders(self):
        template = (ROOT / '.github/RELEASE_TEMPLATE.md').read_text(encoding='utf-8').replace('{VERSION}', '0.3.17')
        self.document.write_text(template, encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'placeholders'):
            self.read()
        template = template.replace('{Release contributors}', 'Community contributors')
        template = template.replace('{One short line per major feature, meaningful change or major fix; 1-14 lines total.}', 'Improved physical combat.')
        template = template.replace('{Group related follow-up fixes; include a useful control or setting path where it fits.}', 'Added a combat setting.')
        self.document.write_text(template, encoding='utf-8')
        self.assertEqual(self.read()['lines'], ['Improved physical combat.', 'Added a combat setting.'])

    def test_generation_updates_after_release_edit(self):
        output = self.root / 'generated/notes.h'
        with patch.object(sys, 'argv', ['release_notes.py', '--notes', str(self.document),
                                      '--identity', str(self.identity), '--output', str(output)]):
            notes.main()
            before = output.read_bytes()
            self.change('First feature.', 'Updated feature.')
            notes.main()
            self.assertNotEqual(before, output.read_bytes())
            self.assertIn('Updated feature.', output.read_text(encoding='utf-8'))

    def test_packaging_refuses_stale_dll_and_mismatched_tag(self):
        data = self.read()
        dll = self.root / 'fixture.dll'
        dll.write_bytes(b'synthetic DLL fixture\0' + notes.marker(data).encode('ascii') + b'\0')
        self.assertEqual(notes.validate_package(data, data['tag'], dll)['version'], '0.3.17')
        with self.assertRaisesRegex(ValueError, 'Package release tag'):
            notes.validate_package(data, 'v0.3.18-windows-preview', dll)
        self.change('First feature.', 'Updated feature.')
        with self.assertRaisesRegex(ValueError, 'stale'):
            notes.validate_package(self.read(), data['tag'], dll)
        dll.write_bytes(b'legacy DLL without summary')
        with self.assertRaisesRegex(ValueError, 'missing or stale'):
            notes.validate_package(data, data['tag'], dll)

    def test_package_command_records_summary_and_rejects_stale_binary_before_output(self):
        data = notes.read_notes(ROOT / 'RELEASE_NOTES.md', ROOT / 'vr/pcvr_identity.h')
        dll = self.root / 'd2rl-pcvr.dll'
        loader = self.root / 'openxr_loader.dll'
        output = self.root / 'package.zip'
        loader.write_bytes(b'synthetic loader fixture')
        argv = ['package.py', '--build', str(self.root), '--openxr-loader', str(loader),
                '--release-tag', data['tag'], '--output', str(output)]
        dll.write_bytes(b'stale fixture')
        with patch.object(sys, 'argv', argv), patch.object(package.subprocess, 'check_output', side_effect=[b'', 'a'*40, 'a'*40]):
            with self.assertRaisesRegex(SystemExit, 'stale'):
                package.main()
        self.assertFalse(output.exists())
        dll.write_bytes(b'synthetic plugin fixture\0' + notes.marker(data).encode('ascii') + b'\0')
        with patch.object(sys, 'argv', argv), patch.object(package.subprocess, 'check_output', side_effect=[b'', 'a'*40, 'a'*40]):
            package.main()
        with zipfile.ZipFile(output) as archive:
            manifest = json.loads(archive.read('d2r-pcvr-preview/release-manifest.json'))
            self.assertEqual(manifest['release_notes']['lines'], data['lines'])
            self.assertEqual(manifest['release_notes']['content_marker'], notes.marker(data))


if __name__ == '__main__':
    unittest.main()
