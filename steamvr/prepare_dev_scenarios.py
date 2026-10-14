"""Create an empty, isolated offline mod environment; never read or edit saves."""
import argparse
from pathlib import Path

MOD = 'pcvr-dev'
MOD_INFO = '{"name":"pcvr-dev","savepath":"pcvr-dev/"}\n'


def prepare(game):
    game = Path(game).resolve()
    if not (game / 'D2R.exe').is_file() or not (game / 'D2RLoader.exe').is_file():
        raise ValueError('Expected a D2R installation with D2RLoader installed.')
    root = game / 'mods' / MOD
    # Do not follow a junction/symlink into another mod or save directory.
    if root.resolve() != root:
        raise ValueError('The development mod path must not contain links or junctions.')
    info = root / f'{MOD}.mpq' / 'modinfo.json'
    if info.resolve() != info:
        raise ValueError('The development metadata path must not contain links or junctions.')
    if root.exists():
        if not info.is_file() or info.read_bytes() != MOD_INFO.encode():
            raise ValueError('Existing pcvr-dev folder has different metadata; refusing to replace it.')
        return root
    info.parent.mkdir(parents=True)
    with info.open('x', encoding='utf-8', newline='\n') as output:
        output.write(MOD_INFO)
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    args = parser.parse_args()
    try:
        root = prepare(args.game)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print(f'Prepared {root}')
    print('Launch the developer DLL with -mod pcvr-dev; create a NEW offline expansion Paladin.')
    print('In the wheel: DEV > Prepare Paladin > Combat gear. Equip the kit, then select Arcane Sanctuary.')


if __name__ == '__main__':
    main()
