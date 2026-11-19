"""Dev only (Lab > Ring wheel > Center > Glyph icon): builds steamvr/assets/ring-wheel/ring-glyph-sheet.png, the painted glyph and sigil squares
on one sheet, and steamvr/ring_wheel_glyph_sheet.h, the mask of which cells are painted.

Jeff 2026-11-18: "glyph icons are claude drawings, lets see what gpt can do to make them look d2 style with the rust and silver, we
need the square as the basic, and then we need to make a circle version for our ring menu". Twelve squares, our own art:
tools/ring_glyph_squares/<kind>-<name>.png (384 px, borderless full-bleed desaturated rust, silver motif in the middle 80%, trimmed to
its alpha box in case of a margin, straight alpha; the 1024 px masters stay in the vault, Design\\ring-wheel\\glyph-icons): the eight
Glyphs-page shapes glyph-{circle,triangle,square,lightning,spiral,v,star,wave} and the four center sigils sigil-{back,clear,star,hand}.

Sheet: 4 columns x 3 rows of 144 px cells, SLOT = the cell, row-major; the names, in slot order, are READ from
steamvr/ring_wheel_glyph_art.h (Names), so the script and the engine cannot disagree on a slot. A cell is the square master,
area-averaged to 144 px; a slot with no square yet leaves its cell fully transparent and its bit clear in PaintedMask, and the engine
keeps that slot's drawn shape or sigil. The sheet and the mask work with 0, 3 or 12 squares, and the same command rebuilds both as
squares land. The compose pass cuts the circle (RecipeSeat) or draws the plate with a frame in code (RecipeSquare) at draw time.

    python tools/ring_glyph_sheet.py [--squares tools/ring_glyph_squares] [--out <png>] [--mask <h>] [--preview <png>]
    python tools/ring_glyph_sheet.py --import <Design\\ring-wheel\\glyph-icons>   (trim + downscale the vault's squares into --squares)
"""
import argparse, os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ART_H = os.path.join(REPO, "steamvr", "ring_wheel_glyph_art.h")
COLS, ROWS, CELL, MASTER = 4, 3, 144, 384


def names(path=ART_H):
    """[(kind, name)] in slot order, from the engine's Names table."""
    text = open(path, encoding="utf-8-sig").read()
    block = text[text.index("Names{{"):]
    block = block[:block.index("}};")]
    found = re.findall(r'\{"([a-z]+)",\s*"([a-z]+)"\}', block)
    assert len(found) == COLS * ROWS, "Names holds %d pictures, the sheet %d" % (len(found), COLS * ROWS)
    return found


def file_name(kind, name):
    return "%s-%s.png" % (kind, name)


def trim(im):
    """The plate's own square: the alpha's bounding box, cut square."""
    im = im.convert("RGBA")
    box = im.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    if not box:
        raise ValueError("fully transparent")
    side = max(box[2] - box[0], box[3] - box[1])
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    x0, y0 = int(round(cx - side / 2)), int(round(cy - side / 2))
    return im.crop((x0, y0, x0 + side, y0 + side))


def import_squares(src, dst):
    os.makedirs(dst, exist_ok=True)
    n = 0
    for kind, name in names():
        p = os.path.join(src, file_name(kind, name))
        if not os.path.exists(p):
            continue
        im = trim(Image.open(p))
        assert im.width == im.height, p
        im.resize((MASTER, MASTER), Image.LANCZOS).save(os.path.join(dst, file_name(kind, name)), optimize=True)
        print("imported", file_name(kind, name)); n += 1
    print("imported %d of %d squares" % (n, COLS * ROWS))


def cell_of(path):
    im = Image.open(path).convert("RGBA")
    assert im.width == im.height, path
    px = im.load()
    pm = Image.new("RGBA", im.size)
    pp = pm.load()
    for y in range(im.height):   # premultiplied area average, then straight again (no dark fringe at the plate's edge)
        for x in range(im.width):
            r, g, b, a = px[x, y]
            pp[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    small = pm.resize((CELL, CELL), Image.BOX if im.width % CELL == 0 else Image.LANCZOS)
    sp = small.load()
    out = Image.new("RGBA", (CELL, CELL))
    op = out.load()
    for y in range(CELL):
        for x in range(CELL):
            r, g, b, a = sp[x, y]
            op[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a) if a else (0, 0, 0, 0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--squares", default=os.path.join(HERE, "ring_glyph_squares"))
    ap.add_argument("--out", default=os.path.join(REPO, "steamvr", "assets", "lab", "ring-glyph-sheet.png"))
    ap.add_argument("--mask", default=os.path.join(REPO, "steamvr", "ring_wheel_glyph_sheet.h"))
    ap.add_argument("--preview", help="also write a check sheet (the sheet on a dark ground)")
    ap.add_argument("--import", dest="imp", help="trim and downscale the vault's painted squares into --squares, then exit")
    a = ap.parse_args()
    if a.imp:
        import_squares(a.imp, a.squares)
        return 0
    sheet = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))
    mask, painted = 0, []
    for slot, (kind, name) in enumerate(names()):
        p = os.path.join(a.squares, file_name(kind, name))
        if not os.path.exists(p):
            continue
        sheet.paste(cell_of(p), ((slot % COLS) * CELL, (slot // COLS) * CELL))
        mask |= 1 << slot
        painted.append("%s-%s" % (kind, name))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sheet.save(a.out, optimize=True)   # PIL writes no text or time chunks unless asked
    head = ("#pragma once\n// Written by tools/ring_glyph_sheet.py: which slots of steamvr/assets/ring-wheel/ring-glyph-sheet.png hold a painted square (bit = slot,\n"
            "// steamvr/ring_wheel_glyph_art.h). Do not edit by hand; the script rewrites it with the sheet.\n#include <cstdint>\n"
            "namespace framevr::ringwheel::glyphart {\nconstexpr uint32_t PaintedMask = 0x%08Xu;   // %d painted of %d\n}\n" % (mask, len(painted), COLS * ROWS))
    with open(a.mask, "w", newline="\n", encoding="utf-8") as f:
        f.write(head)
    if a.preview:
        pv = Image.new("RGBA", (COLS * (CELL + 8) + 8, ROWS * (CELL + 8) + 8), (20, 18, 16, 255))
        for slot in range(COLS * ROWS):
            x, y = 8 + (slot % COLS) * (CELL + 8), 8 + (slot // COLS) * (CELL + 8)
            pv.alpha_composite(sheet.crop(((slot % COLS) * CELL, (slot // COLS) * CELL, (slot % COLS + 1) * CELL, (slot // COLS + 1) * CELL)), (x, y))
        pv.save(a.preview)
    print("wrote %s: %d of %d painted%s" % (os.path.abspath(a.out), len(painted), COLS * ROWS, (" (" + ", ".join(painted) + ")") if painted else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
