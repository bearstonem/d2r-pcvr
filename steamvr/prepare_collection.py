#!/usr/bin/env python3
"""Build the collection catalog from this user's installed game tables.

The catalog is the fixed list behind the Collection tab: one entry per unique,
set item and completed runeword, in a stable order. The panel draws one slot per
entry (an icon when the item is in the shared stash, a greyed outline when it is
not), so the list - not the stash - decides what the tab shows and in what order.

Reads only the game's own tables and writes one JSON file. Standard-library
Python; the resulting file is this user's own data and is never distributed.

Tables come from D2RLoader's extracted compiler folder when present
(<game>/d2rloader/data/compiler/<version>/data/global/excel), which needs no
CASC library; otherwise they are read from the install through game_assets.
"""
import argparse
import json
import re
from pathlib import Path

VERSION = 1

CATEGORIES = ('uniques', 'sets', 'runewords')

UNIQUE_TABLE = 'uniqueitems.txt'
SET_TABLE = 'setitems.txt'
RUNE_TABLE = 'runes.txt'
BASE_TABLES = ('weapons.txt', 'armor.txt', 'misc.txt')


def parse_table(data):
    """Return (header, rows) of a tab-separated D2R table."""
    text = data.decode('utf-8', 'replace')
    lines = text.splitlines()
    if not lines:
        return [], []
    header = [cell_name.strip() for cell_name in lines[0].split('\t')]
    rows = []
    for line in lines[1:]:
        if not line.strip():
            continue
        cells = line.split('\t')
        cells += [''] * (len(header) - len(cells))
        rows.append(dict(zip(header, cells)))
    return header, rows


def cell(row, name, default=''):
    value = row.get(name, default)
    return value.strip() if isinstance(value, str) else default


def number(row, name, default=0):
    try:
        return int(cell(row, name) or default)
    except ValueError:
        return default


def enabled(row):
    """D2R marks a removed row with disabled=1; such rows are not collectable.

    A row without spawnable=1 never drops in game (quest items, unused uniques),
    so it does not belong in a collection either.
    """
    return cell(row, 'disabled') != '1' and cell(row, 'spawnable') == '1'


def code_of(row):
    """The four-character base item code, padded with spaces as D2 stores it."""
    code = cell(row, 'code') or cell(row, 'item')
    return code[:4].ljust(4) if code else ''


def find_excel(game):
    """D2RLoader's extracted excel folder, newest version first, or None."""
    compiler = Path(game) / 'd2rloader' / 'data' / 'compiler'
    if not compiler.is_dir():
        return None
    versions = sorted((p for p in compiler.iterdir() if p.is_dir()),
                      key=lambda p: [int(n) for n in re.findall(r'\d+', p.name)] or [0])
    for version in reversed(versions):
        excel = version / 'data' / 'global' / 'excel'
        if (excel / UNIQUE_TABLE).is_file():
            return excel
    return None


def archive_reader(game, casclib):
    """Read the same tables straight from the install when nothing is extracted."""
    from game_assets import open_assets
    assets = open_assets(game, casclib)

    def read(name):
        return parse_table(assets.read('data/global/excel/' + name))

    return read


def folder_reader(excel):
    def read(name):
        return parse_table((Path(excel) / name).read_bytes())

    return read


def base_types(read):
    """Map a base item code to its item type code (helm, tors, shld, ...)."""
    types = {}
    for name in BASE_TABLES:
        try:
            _, rows = read(name)
        except (FileNotFoundError, ValueError):
            continue
        for row in rows:
            code = cell(row, 'code') or cell(row, 'name')
            if code:
                types[code[:4].ljust(4)] = cell(row, 'type')
    return types


def sort_entries(entries):
    """Type, then level, then name: the order the panel draws, stable per build."""
    return sorted(entries, key=lambda e: (e['type'], e['level'], e['name'], e['code']))


def build(read):
    """The catalog: per category, entries in a stable order.

    recordId is the id the game stores on the item and reports back as
    ItemInfo::qualityRecordId: for uniques and set items the table's *ID column
    (which is not the row position once the table has reserved rows), for a
    completed runeword the Runes.txt row. name is the table's own name text.
    """
    types = base_types(read)
    catalog = {}

    _, uniques = read(UNIQUE_TABLE)
    entries = []
    for row in uniques:
        if not enabled(row) or not code_of(row) or not cell(row, 'index'):
            continue
        entries.append({
            'code': code_of(row),
            'recordId': number(row, '*ID'),
            'name': cell(row, 'index'),
            'level': number(row, 'lvl'),
            'type': types.get(code_of(row), ''),
        })
    catalog['uniques'] = sort_entries(entries)

    _, sets = read(SET_TABLE)
    entries = []
    for row in sets:
        if not enabled(row) or not code_of(row) or not cell(row, 'index'):
            continue
        entries.append({
            'code': code_of(row),
            'recordId': number(row, '*ID'),
            'name': cell(row, 'index'),
            'level': number(row, 'lvl'),
            'type': types.get(code_of(row), ''),
            'setName': cell(row, 'set'),
        })
    catalog['sets'] = sort_entries(entries)

    _, runes = read(RUNE_TABLE)
    entries = []
    for position, row in enumerate(runes):
        if cell(row, 'complete') != '1' or not cell(row, '*Rune Name'):
            continue
        entries.append({
            'code': '',
            'recordId': position,
            'name': cell(row, '*Rune Name'),
            'level': 0,
            'type': cell(row, 'itype1'),
            'runes': [cell(row, 'Rune%d' % n) for n in range(1, 7) if cell(row, 'Rune%d' % n)],
        })
    catalog['runewords'] = sort_entries(entries)

    for category in CATEGORIES:
        for slot, entry in enumerate(catalog[category]):
            entry['slot'] = slot
    return catalog


def write_catalog(catalog, out_dir):
    """Write collection.tsv: one line per entry, tab separated, no dependencies.

    Columns: category, code, recordId, name, level, type, extra (a set's name,
    or a runeword's rune count). The slot is the entry's line number inside its
    category, so the order here is the order the panel draws.
    """
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = ['# collection %d' % VERSION,
             '# category\tcode\trecordId\tname\tlevel\ttype\textra']
    for category in CATEGORIES:
        for entry in catalog[category]:
            extra = entry.get('setName', '') or (' '.join(entry.get('runes', [])) if entry.get('runes') else '')
            lines.append('\t'.join((category, entry['code'].strip(), str(entry['recordId']), entry['name'],
                                    str(entry['level']), entry['type'], extra)))
    (out_dir / 'collection.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return out_dir / 'collection.tsv'


def prepare(game, out_dir, casclib=None):
    """Write collection.tsv and return a summary of what it holds."""
    excel = find_excel(game)
    read = folder_reader(excel) if excel else archive_reader(game, casclib)

    catalog = build(read)
    write_catalog(catalog, out_dir)

    summary = {category: len(catalog[category]) for category in CATEGORIES}
    summary['source'] = str(excel) if excel else 'game archives'
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('.'))
    args = parser.parse_args()
    print(json.dumps(prepare(args.game, args.out), indent=2))


if __name__ == '__main__':
    main()
