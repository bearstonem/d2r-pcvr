#!/usr/bin/env python3
"""The Collection tab on the game's own stash tab bar.

D2R keeps its UI layouts inside its archives rather than in the game folder, so the vanilla
data/global/ui/layouts/bankexpansionlayouthd.json is read from the player's own installation and a
patched copy is shipped in the mod's mpq: a mod overrides a layout by shipping the same path. The
game's own file is never committed or redistributed.

The vanilla bar draws five tabs 195 px wide. Its recessed row runs from 82 to 1084 before the panel's
right border, so a sixth tab does not fit at 195 px, and the tab art is opaque edge to edge, so tabs
cannot overlap one another either. D2R already ships the art for a narrower tab -
data/hd/global/ui/panel/stash/stash_tabs_new.sprite, 167 px, which is what six of them need - so the
bar is switched to that art and the sixth tab is added beside the five. Its label is literal text:
the game's text fields take a string key ("@gems") or the text itself, and there is no "Collection"
key in its tables.

The added tab is a button, not a sixth entry of the tab bar: the bar sends one message for all of its
tabs, and it has no sixth tab to switch to. The button sends PanelManager:TogglePanel:BankExpansionLayout,
a stock message the panel manager handles (a custom message target never reaches a plugin's UI-message
listener), so the open stash closes and the mod's Collection panel takes its place - which is what
choosing that tab means.
"""
import hashlib
import json
from pathlib import Path

from game_assets import open_assets

LAYOUT = 'data/global/ui/layouts/bankexpansionlayouthd.json'
RECEIPT = 'pcvr-collection-tab.json'
BAR = 'BankTabs'
NAME = 'CollectionTab'
LABEL = 'Collection'
MESSAGE = 'PanelManager:TogglePanel:BankExpansionLayout'
TAB_ART = 'PANEL\\\\stash\\\\stash_tabs_new'
# The vanilla bar and the art it is cut for. Every one of these must be present, unchanged, or the
# layout is left alone: a game update that moves the tabs is a reason to look, not to guess.
VANILLA_TABS = '"tabCount": 5,'
VANILLA_RECT = '"rect": { "x": 82, "y": 146 }'
VANILLA_ART = '"filename": "PANEL\\\\stash\\\\MKB_Tabs_195_TGA"'
VANILLA_SIZE = '"tabSize": { "x": 195, "y": 72 }'
# Six tabs of the narrower art: the same left edge, on the row's own height. The narrower art's
# active frame is 10 px taller than the vanilla art's, so the bar rises 4 px to keep it off the top
# row of the grid at y 219.
LEFT, TOP = 82, 142
TAB_WIDTH, TAB_HEIGHT = 167, 78
# The added tab sits after the five, in the bar's own coordinates (its rect is the origin).
SIXTH = 5 * TAB_WIDTH


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


def narrow(block):
    """The bar switches to the game's narrower tab art and leaves room for one more tab."""
    for vanilla, replacement in (
            (VANILLA_ART, f'"filename": "{TAB_ART}"'),
            (VANILLA_SIZE, f'"tabSize": {{ "x": {TAB_WIDTH}, "y": {TAB_HEIGHT} }}'),
            (VANILLA_RECT, f'"rect": {{ "x": {LEFT}, "y": {TOP} }}')):
        if block.count(vanilla) != 1:
            raise ValueError(f'The stash layout changed; the tab bar was left alone: {vanilla}')
        block = block.replace(vanilla, replacement)
    return block


def add_tab(block):
    """One more tab beside the five: a button, so that it can carry the panel message."""
    at = block.find('"fields"')
    if at < 0:
        raise ValueError('The stash tab bar has no fields')
    end = close_of(block, block.index('{', at)) + 1
    children = (
        ',\r\n\t\t\t"children": [\r\n'
        '\t\t\t\t{\r\n'
        f'\t\t\t\t\t"type": "ButtonWidget", "name": "{NAME}",\r\n'
        '\t\t\t\t\t"fields": {\r\n'
        f'\t\t\t\t\t\t"rect": {{ "x": {SIXTH}, "y": 0 }},\r\n'
        f'\t\t\t\t\t\t"filename": "{TAB_ART}",\r\n'
        '\t\t\t\t\t\t"hoveredFrame": 1,\r\n'
        '\t\t\t\t\t\t"pressedFrame": 1,\r\n'
        f'\t\t\t\t\t\t"textString": "{LABEL}",\r\n'
        '\t\t\t\t\t\t"pointSize": "$SmallPanelFontSize",\r\n'
        '\t\t\t\t\t\t"textColor": "$TabsInactiveTextColor",\r\n'
        f'\t\t\t\t\t\t"onClickMessage": "{MESSAGE}"\r\n'
        '\t\t\t\t\t}\r\n'
        '\t\t\t\t}\r\n'
        '\t\t\t]')
    return block[:end] + children + block[end:]


def patch(text):
    """The layout with the Collection tab on its bar; the game's own text is otherwise untouched."""
    start, end = bar(text)
    block = text[start:end]
    if VANILLA_TABS not in block:
        raise ValueError('The stash tab bar does not have the five tabs this expects')
    if '"children"' in block:
        raise ValueError('The stash tab bar already has children; it was left alone')
    return text[:start] + add_tab(narrow(block)) + text[end:]


def generate(assets, mod_name='D2R-PCVR'):
    if mod_name not in ('D2R-PCVR', 'pcvr-dev'):
        raise ValueError('Unsupported Collection tab destination')
    data = assets.read(LAYOUT)
    body = data[len(b'\xef\xbb\xbf'):] if data.startswith(b'\xef\xbb\xbf') else data
    output = {f'{mod_name}.mpq/' + LAYOUT: patch(body.decode('utf-8')).encode('utf-8')}
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
