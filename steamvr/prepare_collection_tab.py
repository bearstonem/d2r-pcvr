#!/usr/bin/env python3
"""The Collection tab on the game's own stash tab bar.

D2R keeps its UI layouts inside its archives rather than in the game folder, so a vanilla layout is
read from the player's own installation and a patched copy is shipped in the mod's mpq: a mod
overrides a layout by shipping the same path. The game's own files are never committed or
redistributed.

The game picks a stash layout by input mode, so two are patched: the base one
(data/global/ui/layouts/bankexpansionlayouthd.json, what a flat session shows) and the controller one
(data/global/ui/layouts/controller/bankexpansionlayouthd.json, what a VR session with controller
navigation shows). Without the second, VR keeps its five tabs.

Each bar draws five tabs of its own art, and the art is opaque edge to edge, so a sixth tab only fits
if the game ships a narrower art for that bar - which it does for both:

- base: five 195 px tabs at y 146; data/hd/global/ui/panel/stash/stash_tabs_new.sprite is 167 px,
  which is what six need. The bar rises 4 px (146 -> 142) because that art's active frame is 10 px
  taller and would otherwise sit on the grid's top row at y 219.
- controller: five 215 px tabs at { 109, -1 };
  data/hd/global/ui/controller/panel/stash/v2/additionalstash/additionalstash_tabs.sprite is 180 px,
  which is what six need (6 x 180 = 1080 from x 109, inside the panel).

The sixth label is literal text: the game's text fields take a string key ("@gems") or the text
itself, and there is no "Collection" key in its tables.

Beside each bar sits one plain button, exactly like the panel's close and gold buttons, drawn at the
sixth tab's own place with the same art: it is what the player clicks. A bar sends one message for all
of its tabs and carries no index in it, so a sixth tab of the bar could not be told from the fifth.
The button sends BankPanelMessage:PCVRCollection - a stock target the game already listens on, with
the mod's own command, which the game's BankPanel ignores. The mod reads it (gamestate.cpp) and shows
the Collection page inside the stash rather than toggling the panel shut.
"""
import hashlib
import json
from pathlib import Path

from game_assets import open_assets

LAYOUT = 'data/global/ui/layouts/bankexpansionlayouthd.json'                     # base (keyboard/mouse)
CONTROLLER_LAYOUT = 'data/global/ui/layouts/controller/bankexpansionlayouthd.json'  # what VR uses
RECEIPT = 'pcvr-collection-tab.json'
BAR = 'BankTabs'
NAME = 'CollectionTab'
LABEL = 'Collection'
# The tab carries a message the mod can tell apart: a stock target the game already
# listens on (BankPanelMessage, whose own tabs send SelectTab) with the mod's own
# command, which the game's BankPanel ignores. So the click shows the Collection
# page inside the stash instead of toggling the panel shut. A wholly custom target
# never reaches a plugin's listener, so the target stays stock.
TARGET = 'BankPanelMessage'
COMMAND = 'PCVRCollection'
MESSAGE = f'{TARGET}:{COMMAND}'

# The base bar: five 195 px tabs at y 146. Every vanilla line below must be present, unchanged, or the
# layout is left alone: a game update that moves the tabs is a reason to look, not to guess.
BASE = {
    'layout': LAYOUT,
    'art': 'PANEL\\\\stash\\\\stash_tabs_new',
    'left': 82, 'top': 142, 'tab_width': 167, 'tab_height': 78,
    'unit': '\t',
    'point_size': '20',
    'vanilla': (
        ('"rect": { "x": 82, "y": 146 }', '"rect": { "x": 82, "y": 142 }'),
        ('"filename": "PANEL\\\\stash\\\\MKB_Tabs_195_TGA"', '"filename": "PANEL\\\\stash\\\\stash_tabs_new"'),
        ('"tabSize": { "x": 195, "y": 72 }', '"tabSize": { "x": 167, "y": 78 }'),
        # The narrower five tabs need a slightly smaller label.
        ('"textStyle": { "pointSize": "$SmallPanelFontSize" }', '"textStyle": { "pointSize": 20 }'),
    ),
}

# The controller bar: five 215 px tabs at { 109, -1 }, four-space indented. The bar keeps its place.
CONTROLLER = {
    'layout': CONTROLLER_LAYOUT,
    'art': 'Controller/Panel/Stash/V2/AdditionalStash/AdditionalStash_Tabs',
    'left': 109, 'top': -1, 'tab_width': 180, 'tab_height': 120,
    'unit': '    ',
    'point_size': '20',
    'vanilla': (
        ('"filename": "Controller/Panel/Stash/V2/AdditionalStash/CTRL_Tabs_215_TGA"',
         '"filename": "Controller/Panel/Stash/V2/AdditionalStash/AdditionalStash_Tabs"'),
        ('"tabSize": { "x": 215, "y": 120 }', '"tabSize": { "x": 180, "y": 120 }'),
        # A 180 px tab needs a smaller label than the 215 px art's own size.
        ('"textStyle": { "pointSize": "$MediumFontSize" }', '"textStyle": { "pointSize": 20 }'),
    ),
}

VARIANTS = (BASE, CONTROLLER)

# Backwards-compatible names for the base variant.
TAB_ART = BASE['art']
VANILLA = BASE['vanilla']
LEFT, TOP = BASE['left'], BASE['top']
TAB_WIDTH, TAB_HEIGHT = BASE['tab_width'], BASE['tab_height']
TAB_X = LEFT + 5 * TAB_WIDTH


def close_of(text, open_at):
    """The index of the brace closing the one at open_at."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('The stash layout has an unbalanced brace')


def bar(text):
    """The tab bar widget's own text."""
    at = text.find(f'"name": "{BAR}"')
    if at < 0:
        raise ValueError('The stash layout has no tab bar')
    start = text.rfind('{', 0, at)
    if start < 0:
        raise ValueError('The stash layout has no tab bar')
    return start, close_of(text, start) + 1


def tab_button(variant):
    """The sixth tab's own button, in the panel's child list, as its close and gold buttons are."""
    unit = variant['unit']
    x = variant['left'] + 5 * variant['tab_width']
    child, field, value = unit * 2, unit * 3, unit * 4
    return (
        f'\r\n{child}{{\r\n'
        f'{field}"type": "ButtonWidget", "name": "{NAME}",\r\n'
        f'{field}"fields": {{\r\n'
        f'{value}"rect": {{ "x": {x}, "y": {variant["top"]} }},\r\n'
        f'{value}"filename": "{variant["art"]}",\r\n'
        f'{value}"hoveredFrame": 1,\r\n'
        f'{value}"pressedFrame": 1,\r\n'
        f'{value}"textString": "{LABEL}",\r\n'
        f'{value}"pointSize": {variant["point_size"]},\r\n'
        f'{value}"textColor": "$TabsInactiveTextColor",\r\n'
        f'{value}"onClickMessage": "{TARGET}:{COMMAND}"\r\n'
        f'{field}}}\r\n'
        f'{child}}},'
    )


def patch_layout(text, variant):
    """One layout with the Collection tab beside its five; the game's own text is otherwise untouched."""
    start, end = bar(text)
    if f'"name": "{NAME}"' in text:
        raise ValueError('The stash layout already has a Collection tab; it was left alone')
    if text[end:end + 1] != ',':
        raise ValueError('The stash tab bar has no widget after it to add to; it was left alone')
    block = text[start:end]
    for find, replace in variant['vanilla']:
        if block.count(find) != 1:
            raise ValueError(f'The stash layout changed; the tab bar was left alone: {find}')
        block = block.replace(find, replace)
    return text[:start] + block + ',' + tab_button(variant) + text[end + 1:]


def patch(text):
    """The base layout (keyboard/mouse) with the Collection tab beside the five."""
    return patch_layout(text, BASE)


def generate(assets, mod_name='D2R-PCVR'):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported Collection tab destination')
    output = {}
    for variant in VARIANTS:
        try:
            data = assets.read(variant['layout'])
        except (KeyError, OSError):
            if variant is BASE:
                raise   # the base stash layout is required; the controller one may be absent
            continue
        body = data[len(b'\xef\xbb\xbf'):] if data.startswith(b'\xef\xbb\xbf') else data
        output[f'{mod_name}.mpq/' + variant['layout']] = patch_layout(body.decode('utf-8'), variant).encode('utf-8')
    record = {'version': 1, 'build': assets.build_hash,
              'sha256': {name: hashlib.sha256(data).hexdigest() for name, data in output.items()}}
    output[RECEIPT] = (json.dumps(record, indent=2) + '\n').encode('utf-8')
    return output


def prepare(game, mod_name='D2R-PCVR', casclib=None):
    """Return a complete payload; the installer's transaction performs writes."""
    with open_assets(Path(game), casclib) as assets:
        return generate(assets, mod_name)


def check_existing(mod, payload):
    """Never replace a player's untracked or edited layout override."""
    mod = Path(mod)
    if mod.resolve() != mod:
        raise ValueError('Collection tab mod folder must not redirect to another location')
    receipt = mod / RECEIPT
    if receipt.resolve() != receipt:
        raise ValueError('Collection tab receipt points outside the mod')
    previous = {}
    if receipt.is_file():
        record = json.loads(receipt.read_text(encoding='utf-8'))
        if not isinstance(record, dict) or record.get('version') != 1 or not isinstance(record.get('sha256'), dict):
            raise ValueError('Unknown Collection tab receipt')
        previous = record['sha256']
    for name, data in payload.items():
        path = mod / name
        if path.resolve() != path:
            raise ValueError('Collection tab destination must not contain links')
        if not path.exists() or name == RECEIPT:
            continue
        old = path.read_bytes()
        if old != data and hashlib.sha256(old).hexdigest() != previous.get(name):
            raise ValueError(f'Existing stash layout left unchanged: {name}')
