"""Native controller health plate with the game's own monster descriptions.

The base layout and all strings/artwork are supplied by the installed game.
MonsterHealthPanel fills Unique and Additional in TargetAttached on SetInfo,
including clearing them on SetNoInfo. No monster statistics are inferred here.
"""
import json
from pathlib import Path


RELATIVE_PATH = Path('data/global/ui/layouts/controller/pc/hudmonsterhealthhd.json')


def description(name, y):
    return {
        'type': 'TextBoxWidget', 'name': name,
        'fields': {
            'priority': 10,
            'rect': {'x': -500, 'y': y, 'width': 1000},
            'style': {
                'pointSize': '$SmallFontSize',
                'alignment': {'h': 'center'},
                'options': {'pipeCharacterBehavior': 'handle'},
            },
        },
    }


def payload():
    # Preserve the PC controller name size and inherit the stock artwork.
    # Backgrounds precede the fill explicitly instead of sharing its priority.
    layout = {
        'basedOn': 'Controller/HUDMonsterHealthHD.json',
        'type': 'MonsterHealthPanel', 'name': 'HUDMonsterHealth',
        'children': [{
            'type': 'Widget', 'name': 'TargetAttached',
            'children': [
                {'type': 'GrowableImageWidget', 'name': 'UniqueMonsterBackground',
                 'fields': {'priority': -20}},
                {'type': 'GrowableImageWidget', 'name': 'NormalMonsterBackground',
                 'fields': {'priority': -20}},
                {'type': 'HealthBarWidget', 'name': 'Health',
                 'fields': {'priority': 0},
                 'children': [{
                     'type': 'TextBoxWidget', 'name': 'Name',
                     'fields': {'style': {'pointSize': '$SmallFontSize'}},
                 }]},
                description('Unique', 30),
                description('Additional', 100),
            ],
        }],
    }
    return (json.dumps(layout, indent=2) + '\n').encode('utf-8')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='Staging .mpq directory; does not install or launch')
    args = parser.parse_args()
    destination = args.output / RELATIVE_PATH
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload())
    print(destination)
