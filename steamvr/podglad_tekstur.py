"""Podglad tekstur gruntu Aktu 3: arkusz do wybrania posadzki pod plyte (main menu, Akt 3).

Wypisuje wszystkie tekstury gruntu Aktu 3 z gry gracza (data/hd/env/texture/terrain/act3/... i reszta tekstur
"terrain" z "act3" w nazwie): kazda jej kolor (_alb) jako kafelek, z numerem; obok, czy ma _nrm i _orm (plyta
potrzebuje wszystkich trzech). Pisze:
  <out>.html - kafelki z nazwami (do ogladania w przegladarce)
  <out>.png  - ten sam arkusz bez napisow, kafelki w rzedach po 10, numerowane od 1 wierszami
  <out>.txt  - numer -> nazwa, rozmiar, czy ma _nrm / _orm
Tylko czyta archiwa gry.

  python podglad_tekstur.py "<folder gry>" --casclib "<...>/CascLib.dll" --out extracted\\podglad\\tekstury_akt3
"""
import argparse
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_assets import open_assets  # noqa: E402
from podglad_modeli import decode_texture, png  # noqa: E402

TILE, COLS = 160, 10


def scale(w, h, rgba, size):
    """Najblizszy sasiad do size x size (kwadrat; tekstury gruntu sa kwadratowe)."""
    out = bytearray(size * size * 4)
    for y in range(size):
        sy = min(h - 1, y * h // size)
        for x in range(size):
            sx = min(w - 1, x * w // size)
            o, i = (y * size + x) * 4, (sy * w + sx) * 4
            out[o:o + 3] = rgba[i:i + 3]
            out[o + 3] = 255
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', type=Path)
    ap.add_argument('--casclib', type=Path, default=None)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out.parent.mkdir(parents=True, exist_ok=True)
    rows, tiles = [], []
    with open_assets(a.game, a.casclib) as assets:
        names = {n.lower(): n for n in assets.files}
        albs = sorted(n for n in names if n.endswith('_alb.texture') and '/texture/' in n and (
            '/terrain/act3/' in n or ('terrain' in n and 'act3' in n)))
        print(f'{len(albs)} tekstur', flush=True)
        for i, n in enumerate(albs, 1):
            stem = n[:-len('_alb.texture')]
            nrm, orm = stem + '_nrm.texture' in names, stem + '_orm.texture' in names
            try:
                w, h, rgba = decode_texture(assets.read(names[n]), 256)
                t = scale(w, h, rgba, TILE)
                info = f'{w}x{h}'
            except Exception as e:   # noqa: BLE001 - tylko podglad
                t, info = bytes([60, 0, 60, 255]) * TILE * TILE, f'nie odczytana ({e})'
            tiles.append(t)
            rows.append((i, names[n], info, nrm, orm))
            if i % 20 == 0:
                print(f'  {i}/{len(albs)}', flush=True)
    n = len(tiles)
    rws = (n + COLS - 1) // COLS
    W, H = COLS * TILE, max(1, rws) * TILE
    sheet = bytearray(W * H * 4)
    for k, t in enumerate(tiles):
        x0, y0 = (k % COLS) * TILE, (k // COLS) * TILE
        for y in range(TILE):
            o = ((y0 + y) * W + x0) * 4
            sheet[o:o + TILE * 4] = t[y * TILE * 4:(y + 1) * TILE * 4]
    a.out.with_suffix('.png').write_bytes(png(W, H, bytes(sheet)))
    a.out.with_suffix('.txt').write_text(
        '\n'.join(f'{i:3}  {nm}  {info}  nrm={"tak" if nr else "NIE"} orm={"tak" if orr else "NIE"}'
                  for i, nm, info, nr, orr in rows) + '\n', encoding='utf-8')
    cells = []
    for (i, nm, info, nr, orr), t in zip(rows, tiles):
        uri = 'data:image/png;base64,' + base64.b64encode(png(TILE, TILE, t)).decode()
        ok = '' if nr and orr else ' style="opacity:.5"'
        cells.append(f'<figure{ok}><img src="{uri}"><figcaption><b>{i}</b> {nm.rsplit("/", 1)[-1][:-12]}'
                     f'<br><small>{nm.rsplit("/", 2)[-2]} · {info}{"" if nr and orr else " · brak nrm/orm"}</small>'
                     f'</figcaption></figure>')
    a.out.with_suffix('.html').write_text(
        '<!DOCTYPE html><meta charset="utf-8"><title>Tekstury gruntu Aktu 3</title><style>'
        'body{background:#1b1915;color:#ddd;font:13px sans-serif}figure{display:inline-block;width:200px;margin:6px;'
        'vertical-align:top}img{width:200px;height:200px;image-rendering:pixelated}small{color:#999}</style>'
        f'<h2>Tekstury gruntu Aktu 3 ({n})</h2>' + ''.join(cells), encoding='utf-8')
    print(f'gotowe - {a.out.with_suffix(".html")}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
