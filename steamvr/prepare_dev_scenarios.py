"""Stage an isolated offline developer mod; never read or edit character saves."""
import argparse
import hashlib
import os
import tempfile
from pathlib import Path
from install import require_game_closed
from game_install import validate_game

MOD = 'pcvr-dev'
MOD_INFO = '{"name":"pcvr-dev","savepath":"pcvr-dev/"}\n'
CAPABILITY = b'Frame development: isolated pcvr-dev environment enabled'
PLUGIN = Path('d2rloader/plugins/d2rl-pcvr.dll')
FILES = (PLUGIN, Path('d2rloader/plugins/d2r_pcvr.ini'),
         Path('d2rloader/plugins/openxr_loader.dll'), Path('reshade/ReShade64.dll'),
         Path('reshade/ReShade.ini'), Path('reshade/D2R_Frame_ReShade.ini'))
TREES = ('d2rloader/plugins/d2r_vr_icons', 'd2rloader/plugins/d2r_vr_potions',
         'reshade/reshade-shaders', 'reshade/D2R_Sky_ours')
# Copied when present; the installer may be unable to prepare them.
OPTIONAL_TREES = ('d2rloader/plugins/d2r_vr_spell_sprites', 'd2rloader/plugins/d2r_vr_props')


def checked(path):
    if path.resolve() != path:
        raise ValueError(f'Developer setup refuses linked paths: {path}')
    return path


def capable(path):
    if not path.is_file() or CAPABILITY not in path.read_bytes():
        raise ValueError('This DLL has no developer tools. Install a dev-capable build or pass --plugin PATH to prepare_dev_scenarios.py.')


def stage(game, plugin=None, experimental_battlenet=False):
    """Copy required mod files locally, preserving all existing settings and saves.

    Launches reuse a complete developer installation. An explicit --plugin is
    the only way this helper replaces an existing DLL, and keeps a hash backup.
    No dependencies are downloaded and no saved character is read or written.
    """
    require_game_closed()
    game = Path(game).resolve()
    validate_game(game, experimental_battlenet=experimental_battlenet)
    root = checked(game / 'mods' / MOD)
    source = checked(game / 'mods' / 'D2R-PCVR')
    for name in ('d2rl-pcvr.dll', 'd2rl-vrcam.dll'):
        if (game / 'd2rloader/plugins' / name).exists():
            raise ValueError('A global VR plugin would load alongside developer mode. Use the current mod-folder installer first.')
    info = checked(root / f'{MOD}.mpq/modinfo.json')
    if root.exists() and (not info.is_file() or info.read_bytes() != MOD_INFO.encode()):
        raise ValueError('Existing pcvr-dev metadata differs; refusing to change it.')
    missing = [p for p in FILES if not checked(root / p).is_file()]
    if not missing and plugin is None:
        capable(root / PLUGIN)
        return root
    candidates = {p: checked(source / p) for p in missing}
    if plugin is not None:
        candidates[PLUGIN] = Path(plugin).resolve()
    capable(candidates.get(PLUGIN, root / PLUGIN))
    if missing:
        trees = [(tree, tree) for tree in TREES]
        trees += [(tree, tree) for tree in OPTIONAL_TREES if checked(source / tree).is_dir()]
        # Locally prepared native assets (e.g. Arcane sky overrides) mount under
        # the selected mod's .mpq folder. Never copy the normal savepath metadata.
        if checked(source / 'D2R-PCVR.mpq/data').is_dir():
            trees.append(('D2R-PCVR.mpq/data', 'pcvr-dev.mpq/data'))
        for tree, destination in trees:
            folder = checked(source / tree)
            if not folder.is_dir():
                raise ValueError(f'Complete normal installation first; missing {folder}')
            for p in folder.rglob('*'):
                checked(p)
                if p.is_file():
                    relative = Path(destination) / p.relative_to(folder)
                    if not checked(root / relative).exists():
                        candidates[relative] = p
    for relative, original in candidates.items():
        checked(root / relative)
        if not original.is_file():
            raise ValueError(f'Complete normal installation first; missing {original}')
    # Validate the entire plan before creating files. prepare preserves metadata.
    prepare(game)
    created = []
    previous = None
    replaced = False
    try:
        require_game_closed()
        for relative, original in candidates.items():
            target = checked(root / relative)
            payload = original.read_bytes()
            if target.exists():
                if relative != PLUGIN or plugin is None:
                    continue
                previous = target.read_bytes()
                if previous == payload:
                    continue
                backup = checked(target.with_name(target.name + '.before-dev-' + hashlib.sha256(previous).hexdigest()[:16]))
                if backup.exists() and backup.read_bytes() != previous:
                    raise ValueError('Developer DLL backup conflicts with existing content.')
                if not backup.exists():
                    with backup.open('xb') as output:
                        output.write(previous)
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.pcvr-dev-', delete=False) as output:
                temporary = Path(output.name)
                output.write(payload)
            try:
                if relative == PLUGIN:
                    require_game_closed()
                if not target.exists():
                    created.append(target)
                else:
                    replaced = True
                os.replace(temporary, target)
                if target.read_bytes() != payload:
                    raise OSError('Developer file verification failed.')
            finally:
                temporary.unlink(missing_ok=True)
        capable(root / PLUGIN)
    except BaseException:
        if replaced and previous is not None:
            (root / PLUGIN).write_bytes(previous)
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    return root


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
    parser.add_argument('--plugin', type=Path, help='Stage/update a built developer-capable DLL; preserve existing settings')
    parser.add_argument('--experimental-battlenet', action='store_true')
    args = parser.parse_args()
    try:
        root = stage(args.game, args.plugin, args.experimental_battlenet)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print(f'Prepared {root}')
    print('Launch with --frame-vr --frame-dev; create a NEW offline expansion character.')
    print('Wheel DEV: Character / Equipment / Act I - III / Act IV - V. VR Settings > DEV: release reference.')


if __name__ == '__main__':
    main()
