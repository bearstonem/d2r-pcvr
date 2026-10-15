#!/usr/bin/env python3
"""Pack the visor blood splatters into assets/blood-splatters.png.

Development only (needs Pillow; SVG sources also need cairosvg). Pass eight
transparent sources in the order `Layout` in blood_screen.h refers to them.
Each is cropped to its visible pixels, scaled to fit a CELL-pixel square with a
margin, centred, and keeps its own alpha. Rebuild the plugin afterwards: the
strip is embedded by interaction_assets.rc.
"""
import argparse
import io
from pathlib import Path
from PIL import Image

CELL = 384
COUNT = 8
MARGIN = 12


def load(path):
    path = Path(path)
    if path.suffix.lower() == '.svg':
        import cairosvg
        return Image.open(io.BytesIO(cairosvg.svg2png(url=str(path), output_width=CELL * 2))).convert('RGBA')
    return Image.open(path).convert('RGBA')


def pack(sources):
    if len(sources) != COUNT:
        raise ValueError(f'need {COUNT} sources, got {len(sources)}')
    strip = Image.new('RGBA', (CELL * COUNT, CELL), (0, 0, 0, 0))
    for index, source in enumerate(sources):
        image = load(source)
        box = image.getbbox()
        if not box:
            raise ValueError(f'{source}: no visible pixels')
        image = image.crop(box)
        scale = (CELL - 2 * MARGIN) / max(image.size)
        image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
        strip.alpha_composite(image, (index * CELL + (CELL - image.width) // 2, (CELL - image.height) // 2))
    return strip


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('sources', nargs=COUNT, help='eight transparent PNG or SVG splatters')
    parser.add_argument('--output', default=str(Path(__file__).with_name('assets') / 'blood-splatters.png'))
    args = parser.parse_args()
    pack(args.sources).save(args.output, optimize=True)
    print(f'Wrote {args.output} ({CELL * COUNT}x{CELL})')


if __name__ == '__main__':
    main()
