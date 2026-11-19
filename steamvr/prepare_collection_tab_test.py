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
        '\t\t\t\t"disabledFrames": [-1, 0, 0, 0, 0 ],',
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


def vanilla_controller():
    """A stand-in for the game's controller stash layout, cut to the fields this reads."""
    return '\r\n'.join([
        '{',
        '    "type": "BankPanel", "name": "BankExpansionLayout",',
        '    "children": [',
        '        {',
        '            "type": "TabBarWidget", "name": "BankTabs",',
        '            "fields": {',
        '                "rect": { "x": 109, "y": -1 },',
        '                "tabCount": 5,',
        '                "filename": "Controller/Panel/Stash/V2/AdditionalStash/CTRL_Tabs_215_TGA",',
        '                "inactiveFrames": [ 0, 0, 0, 0, 0 ],',
        '                "activeFrames": [ 1, 1, 1, 1, 1 ],',
        '                "disabledFrames": [-1, 0, 0, 0, 0 ],',
        '                "textStrings": [ "@personal", "@shared", "@gems", "@materials", "@runes" ],',
        '                "textStyle": { "pointSize": "$MediumFontSize" },',
        '                "tabSize": { "x": 215, "y": 120 },',
        '                "onSwitchTabMessage": "BankPanelMessage:SelectTab",',
        '            }',
        '        },',
        '        {',
        '            "type": "ButtonWidget", "name": "close"',
        '        },',
        '    ]',
        '}',
        ''])


class CollectionTabTests(unittest.TestCase):
    def test_the_bar_keeps_five_tabs_of_the_narrower_art(self):
        # The bar draws five narrower tabs; the sixth is our button, so a click there
        # reaches the button rather than a bar tab that cannot be told from the fifth.
        patched = tab.patch(vanilla())
        self.assertIn('"tabCount": 5,', patched)
        self.assertIn('"filename": "PANEL\\\\stash\\\\stash_tabs_new"', patched)
        self.assertIn('"tabSize": { "x": 167, "y": 78 }', patched)
        self.assertIn('"rect": { "x": 82, "y": 142 }', patched)
        self.assertNotIn('MKB_Tabs_195_TGA', patched)
        self.assertIn('"textStyle": { "pointSize": 20 }', patched)

    def test_the_sixth_tab_is_a_button_beside_the_bar(self):
        patched = tab.patch(vanilla())
        self.assertIn(f'"name": "{tab.NAME}"', patched)
        self.assertIn(f'"onClickMessage": "{tab.MESSAGE}"', patched)
        self.assertIn(f'"textString": "{tab.LABEL}"', patched)
        self.assertIn(f'"rect": {{ "x": {tab.TAB_X}, "y": {tab.TOP} }}', patched)
        # it is the bar's next sibling in the panel, not a child of the bar
        self.assertIn('\t\t},\r\n\t\t{\r\n\t\t\t"type": "ButtonWidget", "name": "CollectionTab",', patched)
        self.assertEqual(tab.TAB_X, tab.LEFT + 5 * tab.TAB_WIDTH)
        self.assertEqual(tab.TAB_X + tab.TAB_WIDTH, 1084)
        self.assertLess(tab.TAB_X + tab.TAB_WIDTH, 1162)

    def test_nothing_outside_the_bar_changes_and_no_bare_line_endings(self):
        text = vanilla()
        patched = tab.patch(text)
        was, now = tab.bar(text), tab.bar(patched)
        self.assertEqual(text[:was[0]], patched[:now[0]])
        # only the added widget sits between the bar and whatever followed it
        inserted = ',' + tab.tab_button(tab.BASE)
        self.assertTrue(patched[now[1]:].startswith(inserted))
        self.assertEqual(text[was[1] + 1:], patched[now[1] + len(inserted):])   # the bar's comma is reused
        self.assertEqual(patched.count('\n'), patched.count('\r\n'))
        self.assertEqual(patched.count('{') - patched.count('}'), 0)
        self.assertEqual(patched.count('[') - patched.count(']'), 0)

    def test_a_layout_that_moved_is_left_alone(self):
        for changed, _ in tab.VANILLA:
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                tab.patch(vanilla().replace(changed, ''))
        # no sibling after the bar, and a tab already there: both are left alone
        with self.assertRaises(ValueError):
            tab.patch(vanilla().replace('\t\t},\r\n\t\t{', '\t\t}\r\n\t\t{'))
        with self.assertRaises(ValueError):
            tab.patch(tab.patch(vanilla()))

    def test_a_layout_without_the_bar_is_left_alone(self):
        with self.assertRaises(ValueError):
            tab.patch('{ "type": "BankPanel", "name": "BankExpansionLayout" }')

    def test_the_patched_layout_is_still_json(self):
        text = re.sub(r',(\s*[\]}])', r'\1', tab.patch(vanilla()))   # the game's file allows trailing commas
        parsed = json.loads(text)
        bar_widget = parsed['children'][0]
        self.assertEqual(bar_widget['name'], 'BankTabs')
        self.assertEqual(bar_widget['fields']['tabCount'], 5)   # the sixth is our button
        self.assertEqual(bar_widget['fields']['textStrings'],
                         ['@personal', '@shared', '@gems', '@materials', '@runes'])
        self.assertEqual(bar_widget['fields']['tabSize'], {'x': 167, 'y': 78})
        self.assertNotIn('children', bar_widget)   # the bar keeps the shape the game already loads
        added = parsed['children'][1]
        self.assertEqual(added['type'], 'ButtonWidget')
        self.assertEqual(added['name'], tab.NAME)
        self.assertEqual(added['fields']['textString'], 'Collection')
        self.assertEqual(added['fields']['onClickMessage'], tab.MESSAGE)
        self.assertEqual(added['fields']['rect'], {'x': tab.TAB_X, 'y': tab.TOP})
        self.assertEqual(parsed['children'][2]['name'], 'close')

    def test_the_controller_bar_keeps_five_tabs_of_its_narrower_art(self):
        patched = tab.patch_layout(vanilla_controller(), tab.CONTROLLER)
        self.assertIn('"tabCount": 5,', patched)
        self.assertIn('"filename": "Controller/Panel/Stash/V2/AdditionalStash/AdditionalStash_Tabs"', patched)
        self.assertIn('"tabSize": { "x": 180, "y": 120 }', patched)
        self.assertNotIn('CTRL_Tabs_215_TGA', patched)
        self.assertEqual(patched.count('\n'), patched.count('\r\n'))   # the controller layout's own CRLF
        parsed = json.loads(re.sub(r',(\s*[\]}])', r'\1', patched))   # the game's file allows trailing commas
        bar_widget = parsed['children'][0]
        self.assertEqual(bar_widget['fields']['tabCount'], 5)   # the sixth is our button
        added = parsed['children'][1]
        self.assertEqual(added['type'], 'ButtonWidget')
        self.assertEqual(added['name'], tab.NAME)
        self.assertEqual(added['fields']['onClickMessage'], tab.MESSAGE)
        self.assertEqual(added['fields']['rect'], {'x': 1009, 'y': -1})   # 109 + 5 x 180

    def test_complete_payload_carries_both_layouts_and_a_receipt(self):
        class Assets:
            build_hash = 'synthetic-build'
            def read(self, name):
                self.asked = name
                return (vanilla() if name == tab.LAYOUT else vanilla_controller()).encode('utf-8')
        for mod in ('D2R-PCVR', 'pcvr-dev'):
            assets = Assets()
            result = tab.generate(assets, mod)
            self.assertEqual(assets.asked, tab.CONTROLLER_LAYOUT)
            self.assertEqual(len(result), 3)   # the base layout, the controller layout, the receipt
            self.assertIn(f'{mod}.mpq/{tab.LAYOUT}', result)
            self.assertIn(f'{mod}.mpq/{tab.CONTROLLER_LAYOUT}', result)
            self.assertIn(b'"tabCount": 5,', result[f'{mod}.mpq/{tab.LAYOUT}'])
            self.assertIn(b'"tabCount": 5,', result[f'{mod}.mpq/{tab.CONTROLLER_LAYOUT}'])
            record = json.loads(result[tab.RECEIPT])
            self.assertEqual(record['sha256'], {name: hashlib.sha256(data).hexdigest()
                                               for name, data in result.items() if name != tab.RECEIPT})

    def test_a_byte_order_mark_is_not_carried_into_the_layout(self):
        class Assets:
            build_hash = 'synthetic-build'
            def read(self, name):
                text = vanilla() if name == tab.LAYOUT else vanilla_controller()
                return b'\xef\xbb\xbf' + text.encode('utf-8')
        result = tab.generate(Assets())
        self.assertFalse(result[f'D2R-PCVR.mpq/{tab.LAYOUT}'].startswith(b'\xef\xbb\xbf'))
        self.assertFalse(result[f'D2R-PCVR.mpq/{tab.CONTROLLER_LAYOUT}'].startswith(b'\xef\xbb\xbf'))

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
