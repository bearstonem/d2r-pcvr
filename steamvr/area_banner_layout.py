"""Place D2R's own area announcement inside the VR view.

Declare both children required by LevelNamePanel explicitly. A same-name
basedOn reference can resolve to this override instead of the root layout,
leaving a panel without children; native Update dereferences them unchecked.
"""
import json
from pathlib import Path


RELATIVE_PATH = Path('data/global/ui/layouts/controller/pc/hudlevelnameshd.json')


def payload():
    def label(name, color, y=0):
        return {'type': 'TextBoxWidget', 'name': name, 'fields': {
            'rect': {'y': y},
            'style': {'pointSize': 54, 'fontColor': color,
                      'alignment': {'h': 'center', 'v': 'bottom'},
                      'dropShadow': '$DefaultDropShadow'},
        }}
    return (json.dumps({
        'type': 'LevelNamePanel',
        'name': 'HUDLevelNames',
        'fields': {'priority': -100, 'anchor': {'x': 0.5, 'y': 0.40},
                   'displaySeconds': 2.5, 'desecratedDisplaySeconds': 4.0,
                   'denOfEvilQuestHack': '@EnteringTheDenOfEvilInQuest'},
        'children': [label('NameText', {'r': 200, 'g': 20, 'b': 0}),
                     label('DesecratedText', '$FontColorDesecrated', 100)],
    }, indent=2) + '\n').encode('utf-8')
