"""Synthetic presets for the campfire clouds fix; no game assets."""
import copy
import json
import tempfile
import unittest
from pathlib import Path

import prepare_campfire_sky as campfire


class Assets:
    build_hash = 'test'
    def __init__(self, preset): self.preset = preset
    town = {'type': 'Preset', 'name': 'townwest', 'entities': []}
    def read(self, name):
        if name == campfire.TOWN:
            return json.dumps(self.town).encode()
        assert name == campfire.PRESET
        return json.dumps(self.preset).encode()


def preset(*names):
    return {'type': 'Preset', 'name': 'charactercreate',
            'entities': [{'type': 'Entity', 'name': n, 'id': i, 'components': []} for i, n in enumerate(names)]}


class CampfireSkyTest(unittest.TestCase):
    def test_takes_out_only_the_clouds(self):
        out = campfire.generate(Assets(preset('ui_skybox_characterSelect_sky', 'FX_UICharacterCreate_clouds_01', 'campfire')), surround=False)
        data = json.loads(out['D2R-PCVR.mpq/' + campfire.PRESET])
        self.assertEqual([e['name'] for e in data['entities']], ['ui_skybox_characterSelect_sky', 'campfire'])
        self.assertEqual(json.loads(out[campfire.RECEIPT])['removed_entities'], ['FX_UICharacterCreate_clouds_01'])

    def test_no_clouds_keeps_the_native_preset(self):
        with self.assertRaises(ValueError):
            campfire.generate(Assets(preset('ui_skybox_characterSelect_sky')))

    def test_an_untracked_override_is_left_alone(self):
        out = campfire.generate(Assets(preset('FX_UICharacterCreate_clouds_01', 'sky')))
        with tempfile.TemporaryDirectory() as folder:
            mod = Path(folder).resolve()
            target = mod / ('D2R-PCVR.mpq/' + campfire.PRESET)
            target.parent.mkdir(parents=True)
            target.write_text('{"edited": true}')
            with self.assertRaises(ValueError):
                campfire.check_existing(mod, out)

    def test_town_turned_behind_the_player(self):
        wall = {'type': 'Entity', 'name': 'ui_wall01_01', 'id': 7, 'components': [
            {'type': 'TransformDefinitionComponent', 'position': {'x': 1020.67, 'y': 1.0, 'z': 123.94},
             'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}},
            {'type': 'ModelDefinitionComponent', 'filename': 'data/hd/env/model/ui/wall.model'}]}
        light = {'type': 'Entity', 'name': 'spot', 'id': 8, 'components': [
            {'type': 'TransformDefinitionComponent', 'position': {'x': 1020.0, 'y': 5.0, 'z': 123.0}},
            {'type': 'SpotLightDefinitionComponent'}]}
        near = copy_of = dict(wall, name='log', id=9, components=[
            {'type': 'TransformDefinitionComponent', 'position': {'x': 1062.0, 'y': 0.0, 'z': 124.0}},
            {'type': 'ModelDefinitionComponent'}])
        src = preset('FX_UICharacterCreate_clouds_01')
        src['entities'] += [wall, light, near]
        data = json.loads(campfire.generate(Assets(src), town_at=None)['D2R-PCVR.mpq/' + campfire.PRESET])
        turned = [e for e in data['entities'] if e['name'].endswith(campfire.MIRROR)]
        self.assertEqual([e['name'] for e in turned], ['ui_wall01_01' + campfire.MIRROR])
        pos = turned[0]['components'][0]['position']
        self.assertAlmostEqual(pos['x'], 1100.67, places=3)
        self.assertAlmostEqual(pos['z'], 123.94, places=3)
        self.assertNotIn(turned[0]['id'], {e['id'] for e in data['entities'] if e is not turned[0]})
        q = turned[0]['components'][0]['orientation']
        self.assertAlmostEqual(abs(q['y']), 1.0)

    def test_harrogath_gate_on_the_scene_gate(self):
        def model(eid, name, x, z):
            return {'type': 'Entity', 'name': name, 'id': eid, 'components': [
                {'type': 'TransformDefinitionComponent', 'position': {'x': x, 'y': 0.0, 'z': z},
                 'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}},
                {'type': 'ModelDefinitionComponent', 'filename': 'data/hd/env/model/x.model'}]}
        assets = Assets(preset('FX_UICharacterCreate_clouds_01'))
        gate = campfire.TOWN_GATE
        # the town's gate itself lands on the scene's gate (its built half: left out); a house 40 units
        # east of the town's fire point lands 40 units east of the scene's fire, behind the player
        at = campfire.TOWN_AT_GATE
        assets.town = {'entities': [model(1, 'wall_gate', gate[0], gate[1]), model(2, 'house', at[0] + 40.0, at[1])]}
        data = json.loads(campfire.generate(assets)['D2R-PCVR.mpq/' + campfire.PRESET])
        placed = {e['name']: e['components'][0]['position'] for e in data['entities'] if e['name'].endswith(campfire.MIRROR)}
        self.assertEqual(list(placed), ['house' + campfire.MIRROR])
        self.assertAlmostEqual(placed['house' + campfire.MIRROR]['x'], campfire.FIRE[0] + 40.0, places=3)
        self.assertAlmostEqual(placed['house' + campfire.MIRROR]['z'], campfire.FIRE[1], places=3)

    def test_harrogath_keeps_off_the_heroes_and_brings_its_ground(self):
        def model(eid, name, x, z):
            return {'type': 'Entity', 'name': name, 'id': eid, 'components': [
                {'type': 'TransformDefinitionComponent', 'position': {'x': x, 'y': 0.0, 'z': z},
                 'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}},
                {'type': 'ModelDefinitionComponent', 'filename': 'data/hd/env/model/x.model'}]}
        assets = Assets(preset('FX_UICharacterCreate_clouds_01'))
        at = campfire.TOWN_AT_GATE
        # a house 35 units off beside the hero row (west, a little behind) stays out; one 70 off stays in
        assets.town = {'entities': [model(1, 'near_house', at[0] - 30.0, at[1] + 18.0),
                                    model(2, 'far_house', at[0] - 60.0, at[1] + 36.0)],
                       'terrain': model(3, 'terrain', 0.0, 0.0)}
        data = json.loads(campfire.generate(assets)['D2R-PCVR.mpq/' + campfire.PRESET])
        placed = {e['name']: e['components'][0]['position'] for e in data['entities'] if e['name'].endswith(campfire.MIRROR)}
        self.assertEqual(sorted(placed), sorted(['far_house' + campfire.MIRROR, 'town_ground' + campfire.MIRROR]))
        ground = placed['town_ground' + campfire.MIRROR]
        self.assertAlmostEqual(ground['x'], campfire.FIRE[0] - at[0], places=3)
        self.assertAlmostEqual(ground['z'], campfire.FIRE[1] - at[1], places=3)
        self.assertLess(ground['y'], 0.0)
        data = json.loads(campfire.generate(assets, ground=False)['D2R-PCVR.mpq/' + campfire.PRESET])
        self.assertFalse(any(e['name'] == 'town_ground' + campfire.MIRROR for e in data['entities']))

    def test_sky_turned_round_the_fire(self):
        base = preset('FX_UICharacterCreate_clouds_01')
        base = json.loads(base) if isinstance(base, (str, bytes)) else copy.deepcopy(base)
        fx, fz = campfire.FIRE
        base['entities'].append({'type': 'Entity', 'name': 'ui_skybox_characterSelect_sky', 'id': 77, 'components': [
            {'type': 'ModelDefinitionComponent', 'filename': 'data/hd/env/model/ui/ui_skybox/characterSelect_sky.model'},
            {'type': 'TransformDefinitionComponent', 'position': {'x': fx - 100.0, 'y': -10.0, 'z': fz},
             'orientation': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'w': 1.0}, 'scale': {'x': 1.0, 'y': 1.0, 'z': 1.0}}]})
        result, added = campfire.with_sky_round(base, (180.0,))
        self.assertEqual(added, 1)
        copy_ = [e for e in result['entities'] if e['name'].endswith(campfire.MIRROR)][0]
        tr = copy_['components'][1]
        # turned about the band's own centre (radius SKY_RADIUS ahead on its +x), not about the fire
        cx = fx - 100.0 + campfire.SKY_RADIUS
        self.assertAlmostEqual(tr['position']['x'], cx + campfire.SKY_RADIUS * (1 + campfire.SKY_GROW), places=3)
        self.assertAlmostEqual(tr['position']['z'], fz, places=3)
        self.assertAlmostEqual(abs(tr['orientation']['y']), 1.0, places=6)
        self.assertGreater(tr['scale']['x'], 1.0)
        again, more = campfire.with_sky_round(result, (180.0,))
        self.assertEqual(more, 1)   # the scene's own sky is the one copied, never a copy

    def test_the_installer_builds_the_tuned_scene(self):
        # v0.3.33 shipped the scene off and the installer built only the plain one (11 changes against the tuned
        # scene's 130 on the owner's game): prepare() now builds TUNED_SCENE unless told otherwise
        seen = {}
        class Opened:
            def __enter__(self): return 'assets'
            def __exit__(self, *a): return False
        def generate(assets, mod_name, **kwargs):
            seen.update(kwargs, mod=mod_name, copies=campfire.COPIES, pave=campfire.PAVE, hide=campfire.HIDE)
            return {'built': b''}
        real = campfire.open_assets, campfire.generate
        campfire.open_assets, campfire.generate = (lambda game, casclib: Opened()), generate
        try:
            self.assertEqual(campfire.prepare('game'), {'built': b''})
        finally:
            campfire.open_assets, campfire.generate = real
        self.assertEqual(seen['mod'], 'D2R-PCVR')
        self.assertTrue(seen['pave'] and len(seen['copies']) > 50 and seen['hide'], 'the tuned scene: paved, its copies, its hidden pieces')
        self.assertEqual((seen['sky_turns'], seen['flip_wall'], seen['shadows_off']), ((), False, False))
        self.assertEqual((campfire.COPIES, campfire.PAVE, campfire.HIDE), ((), False, ()), 'the module settings are put back')

    def test_one_list_of_tuned_options(self):
        script = (Path(__file__).with_name('campfire_scene.ps1')).read_text(encoding='utf-8')
        self.assertIn('--tuned', script)
        self.assertNotIn('--copy', script, 'the script takes TUNED_SCENE, not a second copy of it')
        options = campfire.parse(['game', '--tuned'])
        self.assertTrue(options.tuned)

    def test_the_scene_is_on_by_default(self):
        root = Path(__file__).resolve().parent.parent
        ini = (root / 'd2r_pcvr.ini').read_text(encoding='utf-8')
        section = ini[ini.index('\n[campfire]'):]
        section = section[:section.index('\n[', 2)]
        self.assertIn('\nenabled=1\n', section.replace('\r\n', '\n'))
        self.assertIn('IniB(L"campfire", L"enabled", true)', (root / 'vr/vrcam.cpp').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
