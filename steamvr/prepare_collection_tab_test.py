"""Synthetic layout fixtures only; no game assets or saves."""
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest

import prepare_collection_tab as tab


def vanilla():
    """A stand-in for the game's stash layout, cut to the fields this reads."""
    return '\r\n'.join([
        '{',
        '\t"type": "BankPanel", "name": "BankExpansionLayout",',
        '\t"fields": { "tabNotificationDurationSeconds": 0.33 },',
        '\t"children": [',
        '\t\t{',
        '\t\t\t"type": "TabBarWidget", "name": "BankTabs",',
        '\t\t\t"fields": {',
        '\t\t\t\t"rect": { "x": 82, "y": 146 },',
        '\t\t\t\t"tabCount": 5,',
        '\t\t\t\t"filename": "PANEL\\\\stash\\\\MKB_Tabs_195_TGA",',
        '\t\t\t\t"inactiveFrames": [ 0, 0, 0, 0, 0 ],',
        '\t\t\t\t"activeFrames": [ 1, 1, 1, 1, 1 ],',
        '\t\t\t\t"textStrings": [ "@personal", "@shared", "@gems", "@materials", "@runes" ],',
        '\t\t\t\t"textStyle": { "pointSize": "$SmallPanelFontSize" },',
        '\t\t\t\t"tabSize": { "x": 195, "y": 72 },',
        '\t\t\t\t"onSwitchTabMessage": "BankPanelMessage:SelectTab",',
        '\t\t\t}',
        '\t\t},',
        '\t\t{',
        '\t\t\t"type": "ButtonWidget", "name": "close"',
        '\t\t},',
        '\t]',
        '}',
        ''])


class CollectionTabTests(unittest.TestCase):
    def test_sixth_tab_is_added_with_the_five_untouched(self):
        text = vanilla()
        patched = tab.patch(text)
        self.assertIn(f'"name": "{tab.NAME}"', patched)
        self.assertIn(f'"onClickMessage": "{tab.MESSAGE}"', patched)
        self.assertIn(f'"textString": "{tab.LABEL}"', patched)
        self.assertEqual(patched.count('stash_tabs_new'), 2)   # the five and ours share the art
        self.assertIn('},\r\n\t\t\t"children": [', patched)     # the fields are closed before them
        self.assertIn('"textStrings": [ "@personal", "@shared", "@gems", "@materials", "@runes" ]', patched)

    def test_the_bar_is_re_cut_for_one_more_tab(self):
        patched = tab.patch(vanilla())
        self.assertIn(f'"filename": "{tab.TAB_ART}"', patched)
        self.assertIn(f'"tabSize": {{ "x": {tab.TAB_WIDTH}, "y": {tab.TAB_HEIGHT} }}', patched)
        self.assertIn(f'"rect": {{ "x": {tab.LEFT}, "y": {tab.TOP} }}', patched)
        self.assertNotIn('MKB_Tabs_195_TGA', patched)
        # the sixth tab starts where the five end
        self.assertIn(f'"rect": {{ "x": {tab.SIXTH}, "y": 0 }}', patched)
        self.assertEqual(tab.SIXTH + tab.TAB_WIDTH, 1002)
        self.assertLessEqual(tab.LEFT + 6 * tab.TAB_WIDTH, 1084)

    def test_nothing_outside_the_bar_changes_and_no_bare_line_endings(self):
        text = vanilla()
        patched = tab.patch(text)
        was, now = tab.bar(text), tab.bar(patched)
        self.assertEqual(text[:was[0]], patched[:now[0]])
        self.assertEqual(text[was[1]:], patched[now[1]:])
        self.assertEqual(patched.count('\n'), patched.count('\r\n'))
        self.assertEqual(patched.count('{') - patched.count('}'), 0)
        self.assertEqual(patched.count('[') - patched.count(']'), 0)

    def test_a_layout_that_moved_is_left_alone(self):
        for changed in (tab.VANILLA_TABS, tab.VANILLA_RECT, tab.VANILLA_ART, tab.VANILLA_SIZE):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                tab.patch(vanilla().replace(changed, ''))
        with self.assertRaises(ValueError):
            tab.patch(vanilla().replace('"name": "BankTabs",', '"name": "BankTabs", "children": [],'))

    def test_a_layout_without_the_bar_is_left_alone(self):
        with self.assertRaises(ValueError):
            tab.patch('{ "type": "BankPanel", "name": "BankExpansionLayout" }')

    def test_the_patched_layout_is_still_json(self):
        text = re.sub(r',(\s*[\]}])', r'\1', tab.patch(vanilla()))   # the game's file allows trailing commas
        parsed = json.loads(text)
        bar_widget = parsed['children'][0]
        self.assertEqual(bar_widget['name'], 'BankTabs')
        self.assertEqual(bar_widget['fields']['tabSize'], {'x': tab.TAB_WIDTH, 'y': tab.TAB_HEIGHT})
        self.assertEqual(bar_widget['fields']['textStrings'],
                         ['@personal', '@shared', '@gems', '@materials', '@runes'])
        added = bar_widget['children'][0]['fields']
        self.assertEqual(added['textString'], 'Collection')
        self.assertEqual(added['onClickMessage'], tab.MESSAGE)
        self.assertEqual(added['rect']['x'], tab.SIXTH)
        self.assertEqual(parsed['children'][1]['name'], 'close')

    def test_complete_payload_carries_the_layout_and_a_receipt(self):
        class Assets:
            build_hash = 'synthetic-build'
            def read(self, name):
                self.asked = name
                return vanilla().encode('utf-8')
        for mod in ('D2R-PCVR', 'pcvr-dev'):
            assets = Assets()
            result = tab.generate(assets, mod)
            self.assertEqual(assets.asked, tab.LAYOUT)
            self.assertEqual(len(result), 2)
            self.assertIn(f'{mod}.mpq/{tab.LAYOUT}', result)
            self.assertIn(b'"name": "CollectionTab"', result[f'{mod}.mpq/{tab.LAYOUT}'])
            record = json.loads(result[tab.RECEIPT])
            self.assertEqual(record['sha256'], {name: hashlib.sha256(data).hexdigest()
                                               for name, data in result.items() if name != tab.RECEIPT})

    def test_a_byte_order_mark_is_not_carried_into_the_layout(self):
        class Assets:
            build_hash = 'synthetic-build'
            def read(self, name):
                return b'\xef\xbb\xbf' + vanilla().encode('utf-8')
        self.assertFalse(tab.generate(Assets())[f'D2R-PCVR.mpq/{tab.LAYOUT}'].startswith(b'\xef\xbb\xbf'))

    def test_existing_edits_and_untracked_overrides_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            mod = Path(tmp).resolve()
            name = 'D2R-PCVR.mpq/' + tab.LAYOUT
            path = mod / name
            path.parent.mkdir(parents=True)
            path.write_bytes(b'player layout')
            with self.assertRaisesRegex(ValueError, 'left unchanged'):
                tab.check_existing(mod, {name: b'prepared'})
            self.assertEqual(path.read_bytes(), b'player layout')
            (mod / tab.RECEIPT).write_text(json.dumps(
                {'version': 1, 'sha256': {name: hashlib.sha256(b'previous preparation').hexdigest()}}))
            with self.assertRaisesRegex(ValueError, 'left unchanged'):
                tab.check_existing(mod, {name: b'prepared'})
            path.write_bytes(b'previous preparation')
            tab.check_existing(mod, {name: b'prepared'})
            self.assertEqual(path.read_bytes(), b'previous preparation')   # validation never writes

    def test_unknown_receipt_and_destination_are_rejected(self):
        with self.assertRaises(ValueError):
            tab.generate(None, '../other-mod')
        with tempfile.TemporaryDirectory() as tmp:
            mod = Path(tmp).resolve()
            (mod / tab.RECEIPT).write_text('[]')
            with self.assertRaises(ValueError):
                tab.check_existing(mod, {})


if __name__ == '__main__':
    unittest.main()
