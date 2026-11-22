"""Required native widget contract; no game assets or running game needed."""
import json
import unittest
import area_banner_layout


class BannerLayoutTests(unittest.TestCase):
    def test_native_update_always_has_both_text_widgets(self):
        layout = json.loads(area_banner_layout.payload())
        self.assertEqual(layout['type'], 'LevelNamePanel')
        self.assertNotIn('basedOn', layout)  # No same-name inheritance cycle.
        children = {child['name']: child for child in layout['children']}
        for name in ('NameText', 'DesecratedText'):
            self.assertEqual(children[name]['type'], 'TextBoxWidget')
            self.assertEqual(children[name]['fields']['style']['pointSize'], 54)
        self.assertGreater(layout['fields']['displaySeconds'], 0)
        self.assertGreater(layout['fields']['desecratedDisplaySeconds'], 0)
        self.assertGreater(children['DesecratedText']['fields']['rect']['y'],
                           children['NameText']['fields']['rect']['y'])


if __name__ == '__main__':
    unittest.main()
