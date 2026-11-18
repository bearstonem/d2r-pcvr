"""Podglad modeli D2R w przegladarce: wybrane presety (kafelki mapy) i foldery modeli -> jedna strona HTML z 3D.

Dla wlasciciela (2026-11-14): zobaczyc, jak wygladaja modele (np. wielkie glowy-posagi z pustyni Aktu 2), zanim
wstawimy je do sceny menu. Czyta archiwa gry gracza (tylko do odczytu), zapisuje jedna strone (nie do gita:
wynik to dane gry). Siatki: czytnik Granny z potion_assets; tekstury: albedo (BC1/BC3/RGBA) zmniejszone do ~256 px.

  python podglad_modeli.py "<folder gry>" --casclib "<...>/CascLib.dll" --out "<plik.html>"
         [--preset act2/outdoors/head1 ...] [--folder data/hd/env/model/act2/ruin/act2_ruin_statues ...]
"""
import argparse
import base64
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
import potion_assets  # noqa: E402

PRESETS = ('act2/outdoors/head1', 'act2/outdoors/head2', 'act2/ruin/head3')
FOLDERS = ('data/hd/env/model/act2/ruin/act2_ruin_statues',
           'data/hd/env/model/act2/arcane/act2_arcane_statues',
           'data/hd/env/model/global/prop/objects/statues',
           'data/hd/env/model/act2/outdoors/act2_outdoors_rocks_large')
MAX_VERTS = 45000
TEX_SIZE = 256


class Big(potion_assets.Granny):
    """Czytnik mikstur bez limitow rozmiaru (posagi i skaly sa wieksze)."""

    def array(self, field):
        kind, sub, _, ref = field
        if kind == 7:
            sub = self.ptr(ref)
            ref = (ref[0], ref[1] + 8)
        elif kind not in (3, 4):
            raise ValueError('Expected Granny array')
        count, = self.read(ref, '<I')
        base = self.ptr(ref, 4)
        if count and base is None:
            raise ValueError('Missing Granny array data')
        stride = 8 if kind == 4 else self.definition(sub)[1]
        refs = [(base[0], base[1] + i * stride) for i in range(count)]
        return [self.fields(self.ptr(r) if kind == 4 else r, sub) for r in refs]

    def name_of(self, obj):
        try:
            return self.string(self.ptr(obj['Name'][3]))
        except Exception:   # noqa: BLE001
            return ''

    def all_meshes(self):
        """[(nazwa siatki, [(material, indeksy)], wierzcholki)]; pozycje w prawdziwej skali.

        D2R trzyma pozycje jako bn16 x4: xyz w -1..1, a czwarta skladowa to wykladnik skali (xyz * 2^(13*w)).
        """
        out = []
        for mesh in self.array(self.root['Meshes']):
            vd = self.reference(mesh['PrimaryVertexData'])
            verts = self.array(vd['Vertices'])
            topo = self.reference(mesh['PrimaryTopology'])
            idx = self.array(topo['Indices16']) or self.array(topo['Indices'])
            ix = []
            for i in idx:   # surowo, bez znaku (16-bitowe indeksy > 32767 czytane jako 'h' wychodza ujemne)
                kind, _, _, ref = next(iter(i.values()))
                ix.append(self.read(ref, '<H' if kind in (15, 16, 17, 18, 21) else '<I')[0])
            data = []
            for v in verts:
                p = list(self.scalar(v['Position'])[:3])
                n = self.scalar(v['Normal'])[:3] if 'Normal' in v else (0.0, 1.0, 0.0)
                t = self.scalar(v['TextureCoordinates0'])[:2] if 'TextureCoordinates0' in v else (0.0, 0.0)
                data.append([*p, *n, *t])
            if not data or not ix or len(ix) % 3 or max(ix) >= len(data):
                continue
            # D2R: Position is bn16 x4 - x, y, z in -1..1 and the 4th the scale's exponent: real = xyz * 2^(13 * w)
            # (2026-11-14, granny_bounds.py: tower01 w=10693 -> 18.93 tall, act2_groundplane 17521 -> 123.76, border02,
            # head01 and the menu's ground fit the same rule; the model's own space, as the preset places it)
            pk = verts[0]['Position'][0]
            if pk in (13, 17) and len(self.scalar(verts[0]['Position'])) >= 4:
                for v, src in zip(data, verts):
                    k = 2.0 ** (13.0 * self.scalar(src['Position'])[3])
                    v[0], v[1], v[2] = v[0] * k, v[1] * k, v[2] * k
            mats = []
            try:
                for b in self.array(mesh['MaterialBindings']):
                    mats.append(self.name_of(self.reference(b['Material'])))
            except Exception:   # noqa: BLE001
                pass
            groups = []
            try:
                for g in self.array(topo['Groups']):
                    mi = self.scalar(g['MaterialIndex'])[0]
                    first, count = self.scalar(g['TriFirst'])[0], self.scalar(g['TriCount'])[0]
                    groups.append((mats[mi] if 0 <= mi < len(mats) else '', ix[first * 3:(first + count) * 3]))
            except Exception:   # noqa: BLE001
                groups = []
            if not groups or sum(len(g[1]) for g in groups) != len(ix):
                groups = [(mats[0] if mats else '', ix)]
            out.append((self.name_of(mesh), groups, data))
        return out


# --- tekstury -------------------------------------------------------------------------------------------------

def _c565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def _bc1_block(b, out, w, x0, y0, h, alpha3=True):
    c0, c1, bits = struct.unpack('<HHI', b)
    p0, p1 = _c565(c0), _c565(c1)
    if c0 > c1 or not alpha3:
        pal = [p0 + (255,), p1 + (255,), tuple((2 * a + b_) // 3 for a, b_ in zip(p0, p1)) + (255,),
               tuple((a + 2 * b_) // 3 for a, b_ in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,), tuple((a + b_) // 2 for a, b_ in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    for i in range(16):
        x, y = x0 + (i & 3), y0 + (i >> 2)
        if x < w and y < h:
            o = (y * w + x) * 4
            out[o:o + 4] = bytes(pal[(bits >> (2 * i)) & 3])


def _bc3_block(b, out, w, x0, y0, h):
    a0, a1 = b[0], b[1]
    abits = int.from_bytes(b[2:8], 'little')
    if a0 > a1:
        al = [a0, a1] + [((6 - k) * a0 + k * a1) // 7 for k in range(1, 7)]
    else:
        al = [a0, a1] + [((4 - k) * a0 + k * a1) // 5 for k in range(1, 5)] + [0, 255]
    _bc1_block(b[8:16], out, w, x0, y0, h, alpha3=False)
    for i in range(16):
        x, y = x0 + (i & 3), y0 + (i >> 2)
        if x < w and y < h:
            out[(y * w + x) * 4 + 3] = al[(abits >> (3 * i)) & 7]


def decode_texture(data, want=TEX_SIZE):
    magic, fmt, _, width, height = struct.unpack_from('<IHHII', data)
    mips, = struct.unpack_from('<I', data, 28)
    if magic != 0x2845443c or fmt not in (57, 58, 61, 62, 31):
        raise ValueError(f'format {fmt}')
    level = 0
    while level + 1 < mips and max(width >> level, height >> level) > want:
        level += 1
    w, h = max(1, width >> level), max(1, height >> level)
    size, rel = struct.unpack_from('<II', data, 36 + level * 8)
    off = 40 + level * 8 + rel
    raw = data[off:off + size]
    out = bytearray(w * h * 4)
    if fmt == 31:
        out[:] = raw[:w * h * 4]
    else:
        bs = 8 if fmt in (57, 58) else 16
        bw = (w + 3) // 4
        for by in range((h + 3) // 4):
            for bx in range(bw):
                o = (by * bw + bx) * bs
                blk = raw[o:o + bs]
                if bs == 8:
                    _bc1_block(blk, out, w, bx * 4, by * 4, h)
                else:
                    _bc3_block(blk, out, w, bx * 4, by * 4, h)
    return w, h, bytes(out)


def png(w, h, rgba):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


# --- glowna praca ----------------------------------------------------------------------------------------------

def strip_lod(name):
    return re.sub(r'_lod\d\.model$', '.model', name)


def texture_candidates(model, albs):
    """Albedo pasujace do modelu: data/hd/env/model/<a>/<b>/<folder>/<stem>.model -> .../texture/<a>/<b>/<folder>_<stem>*_alb."""
    parts = model.split('/')
    folder, stem = parts[-2], parts[-1][:-len('.model')]
    key = f'{folder}_{stem}'
    hits = sorted(a for a in albs if a.rsplit('/', 1)[-1].startswith(key))
    if not hits:
        hits = sorted(a for a in albs if a.rsplit('/', 1)[-1].startswith(folder + '_') and stem in a)
    return hits


def pick_texture(mat, cands, albs, model):
    """Albedo dla materialu: najpierw wsrod pasujacych do modelu, potem w calym archiwum (<cokolwiek>_<material>_alb)."""
    ml = (mat or '').lower()
    if not ml:
        return None
    stem = lambda c: c.rsplit('/', 1)[-1][:-len('_alb.texture')]   # noqa: E731
    for c in cands:
        if stem(c).endswith(ml):
            return c
    hits = [c for c in albs if stem(c) == ml or stem(c).endswith('_' + ml)]
    if hits:
        act = model.split('/')[4] if len(model.split('/')) > 4 else ''
        hits.sort(key=lambda c: (act not in c, len(c)))
        return hits[0]
    return None


def convex_hull(pts):
    """The outline of points (x, z) seen from above, anticlockwise, [[x, z], ...] (Andrew's monotone chain)."""
    p = sorted(pts)
    if len(p) < 3:
        return [list(q) for q in p]

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for q in p:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], q) <= 0:
            lower.pop()
        lower.append(q)
    for q in reversed(p):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], q) <= 0:
            upper.pop()
        upper.append(q)
    return [list(q) for q in lower[:-1] + upper[:-1]]


def extract(assets, names, albs, models, report, max_verts=MAX_VERTS, tex_size=TEX_SIZE, log=print):
    """Siatki i tekstury modeli -> ({model: {parts, lo, hi, ...}}, [obrazki data:]). Uzywa tez make_editor.py."""
    out_models, textures, tex_index = {}, [], {}
    for k, m in enumerate(sorted(models), 1):
        stem = m[:-len('.model')]
        chosen = full = None
        for lod in ('_lod0', '_lod1', '_lod2', '', '_lod3', '_lod4'):
            f = stem + lod + '.model'
            if f not in names:
                continue
            try:
                meshes = Big(assets.read(names[f])).all_meshes()
            except Exception as e:   # noqa: BLE001
                report.append(f'  {m}: {f} nie odczytany ({type(e).__name__}: {e})')
                continue
            if not meshes:
                continue
            if full is None:
                full = (f, meshes)   # the most detailed that reads: the model's true size and outline
            if sum(len(d) for _, _, d in meshes) <= max_verts:
                chosen = (f, meshes)
                break
        # (2026-11-15 21:44: when no LOD was light enough the LAST one read was kept - for a menu ground a small piece
        # of it (terrain_act5_lod4: 74 x 31, flat; the ground is ~240 across and rises to 6); now the lightest that fits,
        # else the most detailed - and the bounds always from the most detailed)
        if chosen is None:
            chosen = full
        if chosen and full and chosen is not full:
            def ext(ms):
                xs = [v[0] for _, _, d in ms for v in d]; zs = [v[2] for _, _, d in ms for v in d]
                return (max(xs) - min(xs)) * (max(zs) - min(zs))
            if ext(chosen[1]) < 0.6 * ext(full[1]):   # a light LOD that is only a piece: show the whole
                chosen = full
        if not chosen:
            report.append(f'  {m}: brak siatki')
            continue
        f, meshes = chosen
        flo, fhi = [1e30] * 3, [-1e30] * 3
        pts = set()
        for _, _, d in full[1]:
            for v in d:
                for a in range(3):
                    flo[a], fhi[a] = min(flo[a], v[a]), max(fhi[a], v[a])
                pts.add((round(v[0], 1), round(v[2], 1)))
        hull = convex_hull(pts)
        cands = texture_candidates(m, albs)
        used = []
        parts, lo, hi = [], [1e30] * 3, [-1e30] * 3
        for mname, groups, data in meshes:
            vbytes = base64.b64encode(struct.pack(f'<{len(data) * 8}f', *[x for v in data for x in v])).decode()
            for v in data:
                for a in range(3):
                    lo[a], hi[a] = min(lo[a], v[a]), max(hi[a], v[a])
            for gi, (mat, ix) in enumerate(groups):
                pick = pick_texture(mat, cands, albs, m) or (cands[min(gi, len(cands) - 1)] if cands else None)
                tex = None
                if pick:
                    if pick not in tex_index:
                        try:
                            w, h, rgba = decode_texture(assets.read(names[pick]), tex_size)
                            tex_index[pick] = len(textures)
                            textures.append('data:image/png;base64,' + base64.b64encode(png(w, h, rgba)).decode())
                        except Exception as e:   # noqa: BLE001
                            tex_index[pick] = None
                            report.append(f'  {pick}: tekstura nie odczytana ({e})')
                    tex = tex_index[pick]
                    used.append(pick.rsplit('/', 1)[-1])
                parts.append({'v': vbytes, 'i': base64.b64encode(struct.pack(f'<{len(ix)}I', *ix)).decode(),
                              't': tex, 'mesh': mname, 'mat': mat})
                vbytes = None   # ten sam bufor dla kolejnych grup tej siatki
        used = sorted(set(used))
        lo, hi = flo, fhi
        out_models[m] = {'parts': parts, 'lo': lo, 'hi': hi, 'hull': hull, 'lod': f.rsplit('/', 1)[-1],
                         'from': sorted(models[m]) if isinstance(models, dict) else [], 'tex': used}
        report.append(f'  {m} | {f.rsplit("/", 1)[-1]} (rozmiar z {full[0].rsplit("/", 1)[-1]}, obrys {len(hull)} pkt) | '
                      f'{sum(len(d) for _, _, d in meshes)} wierzch. | '
                      f'rozmiar {hi[0]-lo[0]:.1f} x {hi[1]-lo[1]:.1f} x {hi[2]-lo[2]:.1f} '
                      f'(od {lo[0]:.1f} {lo[1]:.1f} {lo[2]:.1f}) | materialy '
                      f'{[g[0] for _, gs, _ in meshes for g in gs]} | tekstury {used}')
        if k % 10 == 0:
            log(f'  {k}/{len(models)} modeli')
    return out_models, textures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--preset', action='append', default=None)
    ap.add_argument('--folder', action='append', default=None)
    args = ap.parse_args()
    potion_assets.LIMIT = 256 * 1024 * 1024
    presets_in = args.preset or list(PRESETS)
    folders = [f.lower().rstrip('/') + '/' for f in (args.folder or FOLDERS)]
    report, scenes, models = [], {}, {}
    with open_assets(args.game, args.casclib) as assets:
        names = {n.lower(): n for n in assets.files}
        albs = [n for n in names if n.endswith('_alb.texture')]
        for p in presets_in:
            n = f'data/hd/env/preset/{p}.json'
            if n not in names:
                report.append(f'{n}: nie ma w archiwum')
                print(f'{n}: nie ma', flush=True)
                continue
            preset = json.loads(assets.read(names[n]))
            items = []
            for e in preset.get('entities', []):
                tr = mdl = None
                for c in e.get('components', []):
                    if c.get('type') == 'TransformDefinitionComponent':
                        tr = c
                    elif c.get('type') == 'ModelDefinitionComponent' and str(c.get('filename', '')).lower().endswith('.model'):
                        mdl = strip_lod(c['filename'].lower())
                if tr and mdl:
                    pos, q, s = tr['position'], tr['orientation'], tr['scale']
                    items.append({'n': e.get('name', ''), 'm': mdl, 'p': [pos['x'], pos['y'], pos['z']],
                                  'q': [q['x'], q['y'], q['z'], q['w']], 's': [s['x'], s['y'], s['z']]})
                    models.setdefault(mdl, set()).add(p.rsplit('/', 1)[-1])
            scenes[p.rsplit('/', 1)[-1]] = items
            report.append(f'{n}: {len(items)} modeli na kafelku')
            print(f'{n}: {len(items)} modeli', flush=True)
        for f in folders:
            for n in names:
                if n.startswith(f) and n.endswith('.model'):
                    models.setdefault(strip_lod(n), set()).add('folder ' + f.rstrip('/').rsplit('/', 1)[-1])
        print(f'{len(models)} roznych modeli do wyciagniecia', flush=True)
        out_models, textures = extract(assets, names, albs, models, report,
                                       log=lambda t: print(t, flush=True))
    page = (Path(__file__).resolve().parent / 'podglad_modeli_template.html').read_text(encoding='utf-8')
    data = json.dumps({'models': out_models, 'textures': textures, 'scenes': scenes}, separators=(',', ':'))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(page.replace('/*DATA*/null', data), encoding='utf-8')
    args.out.with_suffix('.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print(f'gotowe - {args.out} (i raport {args.out.with_suffix(".txt").name})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
