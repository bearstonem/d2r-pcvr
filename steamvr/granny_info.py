"""Diagnostyka: co jest w pliku .model (Granny) - pola, typy pozycji wierzcholkow, kosci i ich transformacje.

Szukamy, gdzie D2R trzyma prawdziwa skale modelu (pozycje wierzcholkow wychodza znormalizowane do ~1).
  python granny_info.py "<folder gry>" --casclib "<...>/CascLib.dll" <model> [<model> ...] > info.txt
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
import potion_assets  # noqa: E402
from podglad_modeli import Big  # noqa: E402

KINDS = {1: 'inline', 2: 'ref', 3: 'refarray', 4: 'arrayofrefs', 5: 'varref', 7: 'refvararray', 8: 'string',
         9: 'transform', 10: 'f32', 11: 'i8', 12: 'u8', 13: 'bn8', 14: 'un8', 15: 'i16', 16: 'u16', 17: 'bn16',
         18: 'un16', 19: 'i32', 20: 'u32', 21: 'f16', 22: 'empty'}


def show(g, obj, depth, seen, out, ind=''):
    for name, (kind, sub, width, ref) in obj.items():
        k = KINDS.get(kind, kind)
        line = f'{ind}{name}: {k}' + (f'[{width}]' if width > 1 else '')
        try:
            if kind in (10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21):
                vals = g.read(ref, '<' + {10: 'f', 11: 'b', 12: 'B', 13: 'b', 14: 'B', 15: 'h', 16: 'H', 17: 'h',
                                          18: 'H', 19: 'i', 20: 'I', 21: 'e'}[kind] * width)
                line += f' = {[round(v, 4) if isinstance(v, float) else v for v in vals[:16]]}'
            elif kind == 8:
                line += f' = {g.string(g.ptr(ref))!r}'
            elif kind == 9:
                vals = g.read(ref, '<I3f4f9f')
                line += f' flags={vals[0]} pos={[round(v, 4) for v in vals[1:4]]} rot={[round(v, 4) for v in vals[4:8]]} scaleshear={[round(v, 4) for v in vals[8:17]]}'
            out.append(line)
            if depth <= 0:
                continue
            if kind == 2:
                p = g.ptr(ref)
                if p and (p, sub) not in seen:
                    seen.add((p, sub))
                    show(g, g.fields(p, sub), depth - 1, seen, out, ind + '  ')
            elif kind in (3, 4, 7):
                arr = g.array((kind, sub, width, ref))
                out.append(f'{ind}  ({len(arr)} el.)')
                if arr and name not in ('Vertices', 'Indices', 'Indices16'):
                    for el in arr[:3]:
                        show(g, el, depth - 1, seen, out, ind + '    ')
                elif arr:
                    show(g, arr[0], 0, seen, out, ind + '    ')
            elif kind == 1:
                show(g, g.fields(ref, sub), depth - 1, seen, out, ind + '  ')
        except Exception as e:   # noqa: BLE001
            out.append(line + f' (blad: {type(e).__name__}: {e})')


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
            m = m.lower()
            f = m if m in names else m.replace('.model', '_lod0.model')
            print(f'===== {f}')
            g = Big(assets.read(names[f]))
            out = []
            show(g, g.root, 6, set(), out)
            print('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
