#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))   # the embedded runtime adds no script folder
import prepare_frontend_scene as p  # noqa: E402


def scene():
    names = ['act2_sky_01', 'act5_sky_01', 'FX_UIFrontEndAct2_clouds_01', 'FX_UIFrontEndAct5_clouds_02', 'ui_skybox_act3_depth_fade01_01',
             'FX_SandHaze_low_small_gusts_01', 'palm01_01', 'act2_skyline_wall_01']
    return json.dumps({'name': 'frontend', 'entities': [{'name': n, 'components': []} for n in names]}).encode()


class FrontendScene(unittest.TestCase):
    def names(self, payload, mod='D2R-PCVR'):
        return [e['name'] for e in json.loads(payload[f'{mod}.mpq/' + p.PRESET])['entities']]

    def test_clouds_and_sky_cards_go_nothing_else(self):
        out = p.generate(scene())
        self.assertEqual(self.names(out), ['ui_skybox_act3_depth_fade01_01', 'FX_SandHaze_low_small_gusts_01', 'palm01_01', 'act2_skyline_wall_01'])
        record = json.loads(out[p.RECEIPT])
        self.assertEqual(sorted(record['removed_entities']), ['FX_UIFrontEndAct2_clouds_01', 'FX_UIFrontEndAct5_clouds_02', 'act2_sky_01', 'act5_sky_01'])
        self.assertEqual(record['sha256'], {'D2R-PCVR.mpq/' + p.PRESET: p.digest(out['D2R-PCVR.mpq/' + p.PRESET])})

    def test_keep_sky_takes_the_clouds_alone(self):
        self.assertEqual(self.names(p.generate(scene(), keep_sky=True))[:2], ['act2_sky_01', 'act5_sky_01'])

    def test_a_changed_preset_is_left_alone(self):
        with self.assertRaises(ValueError):
            p.generate(json.dumps({'entities': [{'name': 'act2_sky_01'}]}).encode())
        with self.assertRaises(ValueError):
            p.generate(b'{"entities": 3}')
        with self.assertRaises(ValueError):
            p.generate(scene(), mod_name='other')

    def test_an_edited_override_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as d:
            mod = Path(d).resolve()
            out = p.generate(scene(), 'pcvr-dev')
            p.check_existing(mod, out)   # nothing there
            for name, data in out.items():
                (mod / name).parent.mkdir(parents=True, exist_ok=True)
                (mod / name).write_bytes(data)
            p.check_existing(mod, p.generate(scene(), 'pcvr-dev', keep_sky=True))   # ours, by its receipt: replaceable
            (mod / ('pcvr-dev.mpq/' + p.PRESET)).write_bytes(b'{"edited": true}')
            with self.assertRaises(ValueError):
                p.check_existing(mod, out)


if __name__ == '__main__':
    unittest.main()
