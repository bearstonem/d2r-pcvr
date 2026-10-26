#!/usr/bin/env python3
"""Cache original 3D potion meshes and textures, the throwing potions', the arrow's and the thrown traps', from the installed game.

Standard-library Python; Battle.net also needs CascLib on Windows. Cache contains Blizzard
assets and stays in the user's installation, never in the mod's release.
"""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
from game_assets import open_assets
from potion_assets import Granny, texture

VERSION = 4   # the cache; 2 adds the arrow, 3 the thrown traps, 4 the throwing potions
FORMAT = 1    # each .pvr file's layout
CODES = tuple(f'{kind}p{tier}' for kind in 'hm' for tier in range(1, 6)) + ('rvs','rvl','yps','wms','vps')
# The gas and oil potions thrown by hand (#292): each flies as its own bottle
# (the weapon item's model, normalized as the belt potions are).
THROWN = ('gps', 'gpm', 'gpl', 'ops', 'opm', 'opl')
# The basic arrow, drawn on the bow while the game's own nocked arrow is not
# showing (in town the game stops drawing missiles).
ARROW = 'arw'
ARROW_LENGTH = .75   # metres: bowflight::ArrowLength, tail at the nock
# Traps and bombs held in the hand while a throw winds up (#224, toss::Model):
# Fire Blast's bomb, Shock Web's caltrops, Blade Sentinel's blade, the sentry
# (Charged Bolt, Lightning and Death Sentry share it) and the wake (Wake of
# Fire and Wake of Inferno), each from its unit file, TOSS_SIZE across.
TOSS = {'tfb': 'data/hd/missiles/expansion_fire_trauma_missile.json',
        'tsw': 'data/hd/missiles/expansion_caltrops.json',
        'tbs': 'data/hd/character/enemy/bladecreeper.json',
        'tst': 'data/hd/character/enemy/chargeboltsentry.json',
        'twk': 'data/hd/character/enemy/wakeofdestruction.json'}
TOSS_SIZE = .14   # metres: the model's largest side
FILES = tuple(code + '.pvr' for code in CODES + THROWN + (ARROW,) + tuple(TOSS))


def encode(meshes, tex):
    vertices, indices = [], []
    for name, verts, ix in meshes:
        indices.extend(i + len(vertices) for i in ix)
        vertices.extend((*v, 1. if 'glass' in name.lower() else 0.) for v in verts)
    ymin, ymax = min(v[1] for v in vertices), max(v[1] for v in vertices)
    if ymax - ymin < .01 or len(vertices) > 32768 or len(indices) > 131072:
        raise ValueError('Unsupported potion geometry')
    # Authored +Y is the bottle axis. Center the neck over the grip; consistent
    # 18 cm height gives every asset the same mouth anchor used by interactions.
    top = [v for v in vertices if v[1] >= ymax - (ymax-ymin)*.025]
    x, z = (sum(v[a] for v in top) / len(top) for a in (0, 2))
    scale = .18 / (ymax - ymin)
    normalized = [((v[0]-x)*scale, (v[1]-(ymin+ymax)*.5)*scale, (v[2]-z)*scale, *v[3:]) for v in vertices]
    if any(abs(v[a]) > .16 for v in normalized for a in (0,2)):
        raise ValueError('Unexpected potion width')
    return pack(normalized, indices, tex)


def pack(vertices, indices, tex):
    w,h,fmt,mips=tex
    result = bytearray(struct.pack('<8I', 0x31525650, FORMAT, len(vertices), len(indices), w,h,fmt,len(mips)))
    for v in vertices:
        result.extend(struct.pack('<9f', *v))
    result.extend(struct.pack('<' + 'I'*len(indices), *indices))
    for mip in mips:
        result.extend(struct.pack('<I',len(mip)));result.extend(mip)
    return bytes(result)


def encode_arrow(meshes, tex):
    """The arrow along +Y (the game authors it along +Z, tip at +1, tail at
    -.98), centred, ARROW_LENGTH long: the renderer places it by its middle."""
    vertices, indices = [], []
    for _, verts, ix in meshes:
        indices.extend(i + len(vertices) for i in ix)
        vertices.extend(verts)
    if not vertices or len(vertices) > 32768 or len(indices) > 131072:
        raise ValueError('Unsupported arrow geometry')
    lo = [min(v[a] for v in vertices) for a in range(3)]
    hi = [max(v[a] for v in vertices) for a in range(3)]
    if hi[2] - lo[2] < .01:
        raise ValueError('Unsupported arrow geometry')
    scale = ARROW_LENGTH / (hi[2] - lo[2])
    centre = [(lo[a] + hi[a]) * .5 for a in range(3)]
    # (x, y, z) -> (x, z, -y): a quarter turn about x takes +Z to +Y.
    turned = [((v[0]-centre[0])*scale, (v[2]-centre[2])*scale, -(v[1]-centre[1])*scale,
               v[3], v[5], -v[4], v[6], v[7], 0.) for v in vertices]
    if any(abs(v[a]) > .06 for v in turned for a in (0, 2)):
        raise ValueError('Unexpected arrow width')
    return pack(turned, indices, tex)


def encode_toss(meshes, tex):
    """A thrown trap or bomb in the hand: the game's own axes (+Y up), its
    bounding box centred on the origin and its largest side TOSS_SIZE."""
    vertices, indices = [], []
    for _, verts, ix in meshes:
        indices.extend(i + len(vertices) for i in ix)
        vertices.extend(verts)
    if not vertices or len(vertices) > 32768 or len(indices) > 131072:
        raise ValueError('Unsupported thrown model geometry')
    lo = [min(v[a] for v in vertices) for a in range(3)]
    hi = [max(v[a] for v in vertices) for a in range(3)]
    span = max(hi[a] - lo[a] for a in range(3))
    if span < .01:
        raise ValueError('Unsupported thrown model geometry')
    scale = TOSS_SIZE / span
    centre = [(lo[a] + hi[a]) * .5 for a in range(3)]
    return pack([tuple((v[a]-centre[a])*scale for a in range(3)) + tuple(v[3:8]) + (0.,) for v in vertices], indices, tex)


def unit_model(assets, unit_path):
    """A unit file's first model (its _lod0) and albedo: listed with it, else
    beside the model (the missiles keep theirs there)."""
    unit = json.loads(assets.read(unit_path))
    dependencies = unit['dependencies']
    model = dependencies['models'][0]['path'].lower()
    albedo = next((t['path'].lower() for t in dependencies.get('textures', []) if t['path'].lower().endswith('_alb.texture')),
                  model[:-len('.model')] + '_alb.texture')
    return model.replace('.model', '_lod0.model'), albedo


def prepare(game, destination=None, force=False, casclib=None):
    game=Path(game)
    destination=Path(destination) if destination else game/'d2rloader/plugins/d2r_vr_potions'
    destination.mkdir(parents=True,exist_ok=True)
    metadata=destination/'cache.json'
    with open_assets(game, casclib) as assets:
        if not force and metadata.is_file():
            try:
                previous=json.loads(metadata.read_text())
                if previous['version']==VERSION and previous['build']==assets.build_hash and all(
                    hashlib.sha256((destination/(code+'.pvr')).read_bytes()).hexdigest()==previous['sha256'][code] for code in CODES+THROWN+(ARROW,)+tuple(TOSS)):
                    return previous
            except (ValueError,KeyError,OSError):
                pass
        dictionary={k:v for row in json.loads(assets.read('data/hd/items/items.json')) for k,v in row.items()}
        output, sources={},{}
        for code in CODES+THROWN:
            asset=('data/hd/items/weapon/' if code in THROWN else 'data/hd/items/misc/')+dictionary[code]['asset']
            unit=json.loads(assets.read(asset+'.json'))
            model=unit['dependencies']['models'][0]['path'].lower().replace('.model','_lod0.model')
            albedo=next(t['path'].lower() for t in unit['dependencies']['textures'] if t['path'].lower().endswith('_alb.texture'))
            output[code]=encode(Granny(assets.read(model)).meshes(),texture(assets.read(albedo)))
            sources[code]={'model':model,'albedo':albedo}
        unit=json.loads(assets.read('data/hd/missiles/arrow.json'))
        model=unit['dependencies']['models'][0]['path'].lower().replace('.model','_lod0.model')
        albedo=next(t['path'].lower() for t in unit['dependencies']['textures'] if t['path'].lower().endswith('_alb.texture'))
        output[ARROW]=encode_arrow(Granny(assets.read(model)).meshes(),texture(assets.read(albedo)))
        sources[ARROW]={'model':model,'albedo':albedo}
        for code,unit_path in TOSS.items():
            model,albedo=unit_model(assets,unit_path)
            output[code]=encode_toss(Granny(assets.read(model)).meshes(),texture(assets.read(albedo)))
            sources[code]={'model':model,'albedo':albedo}
        # Decode/validate all assets before replacing any live cache files.
        result={'version':VERSION,'build':assets.build_hash,'models':len(CODES),'thrown':len(THROWN),'arrow':True,'toss':len(TOSS),'sources':sources,
                'sha256':{code:hashlib.sha256(data).hexdigest() for code,data in output.items()}}
        for code,data in output.items():
            temp=destination/(code+'.pvr.tmp');temp.write_bytes(data);os.replace(temp,destination/(code+'.pvr'))
        temp=destination/'cache.json.tmp';temp.write_text(json.dumps(result,indent=2)+'\n');os.replace(temp,metadata)
        return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',required=True,type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--force',action='store_true')
    parser.add_argument('--casclib',type=Path,help='Packaged Unicode x64 CascLib 3.0 (Battle.net only)')
    args=parser.parse_args()
    result=prepare(args.game,args.output,args.force,args.casclib)
    print(f"Native potion cache: {result['models']} original 3D models with mipmapped textures")
