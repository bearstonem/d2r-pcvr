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

CATEGORIES = ('uniques', 'sets', 'runewords', 'materials')

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
    return {code: kind for code, (kind, _) in base_items(read).items()}


def base_items(read):
    """Map a base item code to (its item type code, its base name: Shako, Cap, ...)."""
    items = {}
    for name in BASE_TABLES:
        try:
            _, rows = read(name)
        except (FileNotFoundError, ValueError):
            continue
        for row in rows:
            code = cell(row, 'code') or cell(row, 'name')
            if code:
                items[code[:4].ljust(4)] = (cell(row, 'type'), cell(row, 'name'))
    return items


# Quest items the unique table also lists (Amulet of the Viper, the Horadric and
# Kings' staves, the Hell Forge Hammer, both Khalim's Flails): not collectibles.
QUEST_CODES = {'vip', 'hst', 'msf', 'hfh', 'qf1', 'qf2'}


def sort_entries(entries):
    """Type, then level, then name: the order the panel draws, stable per build."""
    return sorted(entries, key=lambda e: (e['type'], e['level'], e['name'], e['code']))


def stat_tables(read):
    """The tables collection_stats words the properties with."""
    tables = {}
    for name in ('properties', 'itemstatcost', 'skills', 'skilldesc', 'monstats'):
        try:
            tables[name] = read(name + '.txt')[1]
        except (FileNotFoundError, ValueError, KeyError):
            tables[name] = []
    return tables


def read_strings(game, casclib=None):
    """(item-modifiers, skill names, item and monster names) from the install's string tables;
    empty when unreadable. The names are the game's own English ones (item-names, item-runes,
    monsters): "Axe of Fechmar", not the table's index "Fechmars Axe" (owner 2026-11-22)."""
    from collection_stats import load_strings
    try:
        from game_assets import open_assets
        assets = open_assets(game, casclib)
        texts = {}
        for name in ('item-modifiers', 'skills', 'item-names', 'item-runes', 'monsters'):
            data = assets.read('data/local/lng/strings/%s.json' % name)
            texts[name] = (data[3:] if data.startswith(b'\xef\xbb\xbf') else data).decode('utf-8')
        return (load_strings([texts['item-modifiers']]), load_strings([texts['skills']]),
                load_strings([texts['item-names'], texts['item-runes'], texts['monsters']]))
    except Exception as error:   # the catalog still builds; the tooltips go without stat lines
        print('collection: no string tables (%s); tooltips without stat lines' % error)
        return {}, {}, {}


def read_rune_name_ids(game, casclib=None):
    """{string key: string id} of item-runes.json: a completed runeword keeps its name's id on
    the item (ItemInfo::prefixIds[0]), the one way to know it when getItemDetails says nothing."""
    import json
    try:
        from game_assets import open_assets
        data = open_assets(game, casclib).read('data/local/lng/strings/item-runes.json')
        rows = json.loads((data[3:] if data.startswith(b'\xef\xbb\xbf') else data).decode('utf-8'))
        return {row['Key']: int(row['id']) for row in rows if 'Key' in row and 'id' in row}
    except Exception as error:
        print('collection: no runeword name ids (%s)' % error)
        return {}


# Runewords made in two kinds of item get a card for each (owner 2026-11-22): the second card's
# recordId is the Runes row plus VARIANT_STEP, and each card's base keeps only its kind's types.
VARIANT_STEP = 1000
RUNEWORD_KINDS = {
    'melee': {'mele', 'swor', 'axe', 'knif', 'pole', 'spea', 'hamm', 'club', 'scep', 'mace', 'h2h', 'wand', 'staf',
              'blde', 'sppl', 'blun', 'rod'},
    'missile': {'miss', 'bow', 'xbow'},
    'swords': {'swor'},
    'shields': {'shld', 'shie', 'ashd', 'pala', 'head', 'grim'},
    'helms': {'helm', 'pelt', 'phlm', 'circ'},
    'body armor': {'tors'},
}
RUNEWORD_KINDS['weapons'] = RUNEWORD_KINDS['melee'] | RUNEWORD_KINDS['missile'] | {'weap', 'orb', 'jave'}
RUNEWORD_SPLITS = {
    'Eternity': ('melee', 'missile'), 'Insight': ('melee', 'missile'), 'Dream': ('helms', 'shields'),
    'Dragon': ('shields', 'body armor'), 'Fortitude': ('weapons', 'body armor'), 'Phoenix': ('weapons', 'shields'),
    'Spirit': ('swords', 'shields'),
}


def split_runewords(entries):
    """Each RUNEWORD_SPLITS runeword as two cards, one per kind, in the order given."""
    out = []
    for entry in entries:
        kinds = RUNEWORD_SPLITS.get(entry['name'])
        if not kinds:
            out.append(entry)
            continue
        types = entry['base'].split()
        for variant, kind in enumerate(kinds):
            own = [t for t in types if t in RUNEWORD_KINDS[kind]]
            if not own:
                continue
            card = dict(entry)
            card['recordId'] = entry['recordId'] + VARIANT_STEP * variant
            card['base'] = ' '.join(own)
            card['type'] = own[0]
            out.append(card)
    return out


# Uniques kept more than once, a card per copy (owner 2026-11-22): Ormus' Robes for its spells,
# Hellfire Torch one per class (the class its "+3 to <class> Skills" rolls, stat 83's layer order).
# A copy's recordId is the item's own plus COPY_STEP times the copy; only the first is counted.
COPY_STEP = 10000
CLASS_NAMES = ('Amazon', 'Sorceress', 'Necromancer', 'Paladin', 'Barbarian', 'Druid', 'Assassin', 'Warlock')
UNIQUE_COPIES = {"Ormus' Robes": None, 'Hellfire Torch': CLASS_NAMES}
ORMUS_COPIES = 5


def copy_uniques(entries):
    out = []
    for entry in entries:
        names = UNIQUE_COPIES.get(entry['name'], ())
        if entry['name'] not in UNIQUE_COPIES:
            out.append(entry)
            continue
        count = ORMUS_COPIES if names is None else len(names)
        for k in range(count):
            card = dict(entry)
            card['recordId'] = entry['recordId'] + COPY_STEP * k
            if names:
                card['name'] = '%s (%s)' % (entry['name'], names[k])
            out.append(card)
    return out


def weapon_hands(read):
    """{base code: 1 one-handed, 2 two-handed} from weapons.txt's 2handed column."""
    try:
        return {cell(row, 'code')[:4].ljust(4): 2 if cell(row, '2handed') == '1' else 1
                for row in read('weapons.txt')[1] if cell(row, 'code')}
    except (FileNotFoundError, ValueError, KeyError):
        return {}


def build(read, strings=None, skill_names=None, names=None):
    """The catalog: per category, entries in a stable order.

    recordId is the id the game stores on the item and reports back as
    ItemInfo::qualityRecordId: for uniques and set items the table's *ID column
    (which is not the row position once the table has reserved rows), for a
    completed runeword the Runes.txt row. name is the table's own name text.
    """
    from collection_stats import stat_lines
    tables = stat_tables(read)
    names = names or {}
    tables['skillnames'] = skill_names or {}
    tables['names'] = names
    stats = lambda row, kind: stat_lines(row, kind, tables, strings or {})
    bases = base_items(read)
    types = {code: kind for code, (kind, _) in bases.items()}
    # the base's shown name: its namestr string, else the table's name
    shown_base = {}
    for table in BASE_TABLES:
        try:
            for row in read(table)[1]:
                code = cell(row, 'code')
                if code and names.get(cell(row, 'namestr')):
                    shown_base[code[:4].ljust(4)] = names[cell(row, 'namestr')]
        except (FileNotFoundError, ValueError, KeyError):
            pass
    base_name = lambda code: shown_base.get(code) or bases.get(code, ('', ''))[1]
    shown = lambda key: names.get(key) or key
    # what the character needs: the row's 'lvl req' (its 'lvl' is the item level - owner
    # 2026-11-22: the Warlock books read 85)
    required = lambda row: number(row, 'lvl req') or number(row, 'lvl')
    hands = weapon_hands(read)
    catalog = {}

    _, uniques = read(UNIQUE_TABLE)
    entries = []
    for row in uniques:
        if not enabled(row) or not code_of(row) or not cell(row, 'index'):
            continue
        if code_of(row).strip() in QUEST_CODES:
            continue
        entries.append({
            'code': code_of(row),
            'recordId': number(row, '*ID'),
            'name': shown(cell(row, 'index')),
            'level': required(row),
            'type': types.get(code_of(row), ''),
            'base': base_name(code_of(row)),
            'hands': hands.get(code_of(row), 0),
            'stats': stats(row, 'unique'),
        })
    catalog['uniques'] = sort_entries(copy_uniques(entries))

    # The set bonuses (owner 2026-11-22): a piece's own green lines with 2, 3 ... pieces worn
    # (SetItems aprop1..5 a/b: "i2:".."i6:"), and the set's (Sets PCode2..5 a/b: "s2:".."s5:", and
    # FCode1..8 for the whole set: "sF:"), each line its pieces' count before a colon.
    def prop_lines(props):
        row = {}
        for i, (code, par, lo, hi) in enumerate(props, 1):
            row.update({'prop%d' % i: code, 'par%d' % i: par, 'min%d' % i: lo, 'max%d' % i: hi})
        return stat_lines(row, 'unique', tables, strings or {}) if props else []
    try:
        set_rows = {cell(r, 'index'): r for r in read('sets.txt')[1]}
    except (FileNotFoundError, ValueError, KeyError):
        set_rows = {}
    def set_bonus(set_key):
        row = set_rows.get(set_key)
        if not row:
            return []
        out = []
        for n in range(2, 6):
            props = [(cell(row, 'PCode%d%s' % (n, s)), cell(row, 'PParam%d%s' % (n, s)), cell(row, 'PMin%d%s' % (n, s)),
                      cell(row, 'PMax%d%s' % (n, s))) for s in 'ab' if cell(row, 'PCode%d%s' % (n, s))]
            out += ['s%d:%s' % (n, line) for line in prop_lines(props)]
        props = [(cell(row, 'FCode%d' % n), cell(row, 'FParam%d' % n), cell(row, 'FMin%d' % n), cell(row, 'FMax%d' % n))
                 for n in range(1, 9) if cell(row, 'FCode%d' % n)]
        return out + ['sF:%s' % line for line in prop_lines(props)]
    def piece_bonus(row):
        out = []
        for n in range(1, 6):
            props = [(cell(row, 'aprop%d%s' % (n, s)), cell(row, 'apar%d%s' % (n, s)), cell(row, 'amin%d%s' % (n, s)),
                      cell(row, 'amax%d%s' % (n, s))) for s in 'ab' if cell(row, 'aprop%d%s' % (n, s))]
            out += ['i%d:%s' % (n + 1, line) for line in prop_lines(props)]
        return out

    _, sets = read(SET_TABLE)
    entries = []
    for row in sets:
        if not enabled(row) or not code_of(row) or not cell(row, 'index'):
            continue
        entries.append({
            'code': code_of(row),
            'recordId': number(row, '*ID'),
            'name': shown(cell(row, 'index')),
            'level': required(row),
            'type': types.get(code_of(row), ''),
            'setName': shown(cell(row, 'set')),
            'bonus': piece_bonus(row) + set_bonus(cell(row, 'set')),
            'base': base_name(code_of(row)),
            'hands': hands.get(code_of(row), 0),
            'stats': stats(row, 'set'),
        })
    catalog['sets'] = sort_entries(entries)

    _, misc = read('misc.txt')
    # A runeword asks the level of its most demanding rune (the game's own rule).
    rune_level = {cell(row, 'code'): number(row, 'levelreq') for row in misc if cell(row, 'type') == 'rune'}
    _, runes = read(RUNE_TABLE)
    entries = []
    for position, row in enumerate(runes):
        if cell(row, 'complete') != '1' or not cell(row, '*Rune Name'):
            continue
        entries.append({
            'code': '',
            'recordId': position,
            'nameKey': cell(row, 'Name'),   # its string key; prepare() adds the string's id (nameId)
            'name': names.get(cell(row, 'Name')) or cell(row, '*Rune Name'),
            'level': max([rune_level.get(cell(row, 'Rune%d' % n), 0) for n in range(1, 7)] or [0]),
            'type': cell(row, 'itype1'),
            # the item types it can be made in (the panel draws them on the card)
            'base': ' '.join(cell(row, 'itype%d' % n) for n in range(1, 7) if cell(row, 'itype%d' % n)),
            'runes': [cell(row, 'Rune%d' % n) for n in range(1, 7) if cell(row, 'Rune%d' % n)],
            'stats': stats(row, 'runeword'),
        })
    catalog['runewords'] = sort_entries(split_runewords(entries))

    # Gems and runes: base items, so their own record id is zero and the storage
    # layer matches them by their code. They are stackable, so the tab shows one
    # slot per kind rather than one per copy.
    entries = []
    for row in misc:
        kind = cell(row, 'type')
        if not (kind.startswith('gem') or kind == 'rune'):
            continue
        code = cell(row, 'code')
        if not code:
            continue
        entries.append({
            'code': code[:4].ljust(4),
            'recordId': 0,
            'name': cell(row, 'name'),
            'level': number(row, 'level'),
            'type': kind,
        })
    catalog['materials'] = sort_entries(entries)

    for category in CATEGORIES:
        for slot, entry in enumerate(catalog[category]):
            entry['slot'] = slot
    return catalog


def write_catalog(catalog, out_dir):
    """Write collection.tsv: one line per entry, tab separated, no dependencies.

    Columns: category, code, recordId, name, level, type, extra (a set's name,
    or a runeword's rune count), base (the base item's name: Shako, Cap, ...),
    stats (the tooltip's stat lines with their ranges, joined by '|'), nameId (a runeword's
    name string id, which the game keeps on the item as prefixIds[0]; 0 for the rest).
    The slot is the entry's line number inside its category, so the order here
    is the order the panel draws.
    """
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = ['# collection %d' % VERSION,
             '# category\tcode\trecordId\tname\tlevel\ttype\textra\tbase\tstats\tnameId\thands\tbonus']
    for category in CATEGORIES:
        for entry in catalog[category]:
            extra = entry.get('setName', '') or (' '.join(entry.get('runes', [])) if entry.get('runes') else '')
            stat_text = '|'.join(line.replace('|', '/').replace('\t', ' ') for line in entry.get('stats', []))
            lines.append('\t'.join((category, entry['code'].strip(), str(entry['recordId']), entry['name'],
                                    str(entry['level']), entry['type'], extra, entry.get('base', ''), stat_text,
                                    str(entry.get('nameId', 0)), str(entry.get('hands', 0)),
                                    '|'.join(line.replace('|', '/').replace('\t', ' ') for line in entry.get('bonus', [])))))
    (out_dir / 'collection.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return out_dir / 'collection.tsv'


def prepare(game, out_dir, casclib=None):
    """Write collection.tsv and return a summary of what it holds."""
    excel = find_excel(game)
    read = folder_reader(excel) if excel else archive_reader(game, casclib)

    strings, skill_names, names = read_strings(game, casclib)
    catalog = build(read, strings, skill_names, names)
    name_ids = read_rune_name_ids(game, casclib)
    for entry in catalog['runewords']:
        entry['nameId'] = name_ids.get(entry.get('nameKey', ''), 0)
    write_catalog(catalog, out_dir)

    summary = {category: len(catalog[category]) for category in CATEGORIES}
    summary['source'] = str(excel) if excel else 'game archives'
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('.'))
    parser.add_argument('--casclib', type=Path, default=None, help='CascLib.dll, for the string tables')
    args = parser.parse_args()
    print(json.dumps(prepare(args.game, args.out, args.casclib), indent=2))


if __name__ == '__main__':
    main()
