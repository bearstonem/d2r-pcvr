"""Diagnostyka rozmiarow modeli: surowe pozycje wierzcholkow (typ, min/max kazdej skladowej, 4. skladowa),
OBB z BoneBindings, kosci i InitialPlacement - zeby ustalic, jak D2R zapisuje prawdziwy rozmiar modelu.

  python granny_bounds.py "<folder gry>" --casclib "<...>/CascLib.dll" <model> [<model> ...] > bounds.txt
(model bez _lodN - sprawdzane sa wszystkie lod-y, ktore sa w archiwum)
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
import potion_assets  # noqa: E402
from podglad_modeli import Big  # noqa: E402

FMT = {10: 'f', 11: 'b', 12: 'B', 13: 'b', 14: 'B', 15: 'h', 16: 'H', 17: 'h', 18: 'H', 19: 'i', 20: 'I', 21: 'e'}


def raw(g, field):
    kind, _, width, ref = field
    return kind, g.read(ref, '<' + FMT[kind] * width)


def r4(v):
    return [round(x, 4) if isinstance(x, float) else x for x in v]


def one(g, out):
    for mi, mesh in enumerate(g.array(g.root['Meshes'])):
        vd = g.reference(mesh['PrimaryVertexData'])
        verts = g.array(vd['Vertices'])
        if not verts:
            continue
        kind, first = raw(g, verts[0]['Position'])
        n = len(first)
        lo, hi = list(first), list(first)
        for v in verts:
            _, p = raw(g, v['Position'])
            for a in range(n):
                lo[a], hi[a] = min(lo[a], p[a]), max(hi[a], p[a])
        out.append(f'  siatka {mi} {g.name_of(mesh)!r}: {len(verts)} wierzch., Position typ {kind} x{n}, '
                   f'min {r4(lo)} max {r4(hi)}, pierwszy {r4(first)}')
        out.append(f'    pola wierzcholka: {list(verts[0].keys())}')
        try:
            for b in g.array(mesh['BoneBindings']):
                out.append(f'    BoneBinding {g.name_of(b)!r}: OBBMin {r4(g.scalar(b["OBBMin"]))} OBBMax {r4(g.scalar(b["OBBMax"]))}')
        except Exception as e:   # noqa: BLE001
            out.append(f'    BoneBindings: blad {e}')
    try:
        for m in g.array(g.root['Models']):
            pl = m.get('InitialPlacement')
            if pl:
                v = g.read(pl[3], '<I3f4f9f')
                out.append(f'  model {g.name_of(m)!r}: InitialPlacement pos {r4(v[1:4])} rot {r4(v[4:8])} skala {r4(v[8:17])}')
            sk = g.reference(m['Skeleton'])
            for b in g.array(sk['Bones']):
                lt = g.read(b['LocalTransform'][3], '<I3f4f9f')
                iw = g.read(b['InverseWorldTransform'][3], '<16f')
                out.append(f'    kosc {g.name_of(b)!r}: local pos {r4(lt[1:4])} rot {r4(lt[4:8])} skala {r4(lt[8:17])}; invworld {r4(iw)}')
    except Exception as e:   # noqa: BLE001
        out.append(f'  modele/kosci: blad {type(e).__name__}: {e}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('models', nargs='+')
    args = ap.parse_args()
    potion_assets.LIMIT = 256 * 1024 * 1024
    with open_assets(args.game, args.casclib) as assets:
        names = {n.lower(): n for n in assets.files}
        for m in args.models:
            stem = m.lower().replace('.model', '')
            for lod in ('', '_lod0', '_lod1', '_lod2', '_lod3', '_lod4'):
                f = stem + lod + '.model'
                if f not in names:
                    continue
                out = [f'===== {f}']
                try:
                    one(Big(assets.read(names[f])), out)
                except Exception as e:   # noqa: BLE001
                    out.append(f'  nie odczytany: {type(e).__name__}: {e}')
                print('\n'.join(out), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
