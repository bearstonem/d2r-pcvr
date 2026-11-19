"""Offline tool (not run by the build): builds steamvr/assets/ring-wheel/ring-tree-sheet.png, the painted tree squares
on one sheet, and steamvr/ring_wheel_tree_sheet.h, the mask of which cells are painted.

One painted square per skill tree, 24 in all, original art kept by the artist (not in the repo):
<squares>/tree-<class>-<tree>.png (384 px, borderless full-bleed rust (no frame, bevel or rivets; motif in the middle 80%), trimmed to its alpha box in case of a margin, straight alpha; the 1024 px masters
stay with the artist).

Sheet: 6 columns x 4 rows of 144 px cells (the ring's atlas art square at 150%), SLOT = the cell, row-major, SLOT = class * 3 +
the tree's skillcategories page; the class and tree names, in slot order, are READ from steamvr/ring_wheel_tree_art.h (Names), so
the script and the engine cannot disagree on a slot. A cell is the square master, area-averaged to 144 px; a tree with no square
yet leaves its cell fully transparent and its bit clear in PaintedMask, and the engine keeps that tree's drawn glyph. The sheet
and the mask therefore work with 0, 3 or 24 squares, and the same command rebuilds both as squares land.

The compose pass cuts the circle at draw time the way it cuts every skill (RecipeSeat: our dark disc, the art cut round, the rim
shade, the dark line, the rim) from the whole square (nothing to crop: no bevel in the art); the square mode draws the whole square
and adds the plate's frame in code (RecipeSquare: a bevel, a dark line, the accent rim). Nothing here is hand-painted round art.

    python tools/ring_tree_sheet.py [--squares <folder of squares>] [--out <png>] [--mask <h>] [--preview <png>]
    python tools/ring_tree_sheet.py --import <folder of squares>   (trim + downscale the squares into --squares)
"""
import argparse, os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ART_H = os.path.join(REPO, "steamvr", "ring_wheel_tree_art.h")
COLS, ROWS, CELL, MASTER = 6, 4, 144, 384


def tree_names(path=ART_H):
    """[(class, tree)] in slot order, from the engine's Names table."""
    text = open(path, encoding="utf-8-sig").read()
    block = text[text.index("Names{{"):]
    block = block[:block.index("}};")]
    names = re.findall(r'\{"([a-z-]+)",\s*"([a-z-]+)"\}', block)
    assert len(names) == COLS * ROWS, "Names holds %d trees, the sheet %d" % (len(names), COLS * ROWS)
    return names


def file_name(cls, tree):
    return "tree-%s-%s.png" % (cls, tree)


def trim(im):
    """The plate's own square: the alpha's bounding box (the master has an empty margin outside x = 31..994), cut square."""
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
    for cls, tree in tree_names():
        p = os.path.join(src, file_name(cls, tree))
        if not os.path.exists(p):
            continue
        im = trim(Image.open(p))
        assert im.width == im.height, p
        im.resize((MASTER, MASTER), Image.LANCZOS).save(os.path.join(dst, file_name(cls, tree)), optimize=True)
        print("imported", file_name(cls, tree)); n += 1
    print("imported %d of %d squares" % (n, COLS * ROWS))


def cell_of(path):
    im = Image.open(path).convert("RGBA")
    assert im.width == im.height, path
    # premultiplied area average, then straight again (no dark fringe at the plate's edge)
    px = im.load()
    pm = Image.new("RGBA", im.size)
    pp = pm.load()
    for y in range(im.height):
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
    ap.add_argument("--squares", default=os.path.join(HERE, "ring_tree_squares"))
    ap.add_argument("--out", default=os.path.join(REPO, "steamvr", "assets", "ring-wheel", "ring-tree-sheet.png"))
    ap.add_argument("--mask", default=os.path.join(REPO, "steamvr", "ring_wheel_tree_sheet.h"))
    ap.add_argument("--preview", help="also write a check sheet (the sheet on a dark ground with the slot numbers' empties marked)")
    ap.add_argument("--import", dest="imp", help="trim and downscale the painted squares into --squares, then exit")
    a = ap.parse_args()
    if a.imp:
        import_squares(a.imp, a.squares)
        return 0
    names = tree_names()
    sheet = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))
    mask, painted = 0, []
    for slot, (cls, tree) in enumerate(names):
        p = os.path.join(a.squares, file_name(cls, tree))
        if not os.path.exists(p):
            continue
        sheet.paste(cell_of(p), ((slot % COLS) * CELL, (slot // COLS) * CELL))
        mask |= 1 << slot
        painted.append(file_name(cls, tree)[5:-4])
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sheet.save(a.out, optimize=True)   # PIL writes no text or time chunks unless asked
    head = ("#pragma once\n// Written by tools/ring_tree_sheet.py: which slots of steamvr/assets/ring-wheel/ring-tree-sheet.png hold a painted square (bit = slot,\n"
            "// steamvr/ring_wheel_tree_art.h). Do not edit by hand; the script rewrites it with the sheet.\n#include <cstdint>\n"
            "namespace framevr::ringwheel::treeart {\nconstexpr uint32_t PaintedMask = 0x%08Xu;   // %d painted of %d\n}\n" % (mask, len(painted), COLS * ROWS))
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
