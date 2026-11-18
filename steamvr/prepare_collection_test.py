"""Checks for prepare_collection: the catalog the Collection tab draws."""
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from prepare_collection import build, folder_reader, parse_table, prepare

UNIQUES = (
    'index\t*ID\tversion\tdisabled\tspawnable\t*ItemName\tlvl\tcode\n'
    'The Gnasher\t0\t0\t\t1\tHand Axe\t7\thax\n'
    'Removed One\t1\t0\t1\t1\tAxe\t1\thax\n'
    'Never Drops\t2\t0\t\t\tRing\t1\trin\n'
    'Reserved Id\t9\t0\t\t1\tRing\t39\trin\n'
    'No Code At All\t10\t0\t\t1\tRing\t1\t\n'
    'The Stone of Jordan\t122\t0\t\t1\tRing\t39\trin\n'
)

SETS = (
    'index\t*ID\tset\tdisabled\tspawnable\t*ItemName\tlvl\titem\n'
    "Civerb's Ward\t0\tCiverb's Vestments\t\t1\tLarge Shield\t3\tlrg\n"
    "Removed Set Item\t1\tGone\t1\t1\tAmulet\t1\tamu\n"
)

RUNES = (
    'Name\t*Rune Name\tcomplete\titype1\tRune1\tRune2\tRune3\n'
    'Runeword1\tAncients\u0027 Pledge\t1\tshld\tr08\tr09\tr07\n'
    'Runeword2\tUnfinished\t\tshld\tr01\t\t\n'
    'Runeword3\tBeast\t1\taxe\tr30\tr03\tr22\n'
)

WEAPONS = 'name\ttype\tcode\nHand Axe\taxe\thax\nBerserker Axe\taxe\t7wa\n'
ARMOR = 'name\ttype\tcode\nCap\thelm\tcap\n'
MISC = 'name\ttype\tcode\nRing\tring\trin\nLarge Shield\tshld\tlrg\nAmulet\tamul\tamu\n'


def tables(root):
    excel = Path(root) / 'd2rloader' / 'data' / 'compiler' / '9.9.9' / 'data' / 'global' / 'excel'
    excel.mkdir(parents=True)
    for name, text in (('uniqueitems.txt', UNIQUES), ('setitems.txt', SETS), ('runes.txt', RUNES),
                       ('weapons.txt', WEAPONS), ('armor.txt', ARMOR), ('misc.txt', MISC)):
        (excel / name).write_text(text, encoding='utf-8')
    return excel


class TestPrepareCollection(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.game = Path(self.temporary.name)
        self.excel = tables(self.game)

    def test_parse_table_pads_short_rows(self):
        header, rows = parse_table(b'index\t*ID\tcode\nOne\t1\n')
        self.assertEqual(header, ['index', '*ID', 'code'])
        self.assertEqual(rows[0]['code'], '')

    def test_catalog_skips_rows_that_never_drop_or_have_no_item(self):
        catalog = build(folder_reader(self.excel))
        names = [entry['name'] for entry in catalog['uniques']]
        self.assertNotIn('Removed One', names)      # disabled
        self.assertNotIn('Never Drops', names)      # not spawnable
        self.assertNotIn('No Code At All', names)   # no base item
        self.assertIn('The Gnasher', names)
        self.assertIn('The Stone of Jordan', names)
        self.assertEqual(len(catalog['uniques']), 3)

    def test_record_id_is_the_id_column_not_the_row(self):
        catalog = build(folder_reader(self.excel))
        by_name = {entry['name']: entry for entry in catalog['uniques']}
        self.assertEqual(by_name['Reserved Id']['recordId'], 9)
        self.assertEqual(by_name['The Stone of Jordan']['recordId'], 122)

    def test_type_comes_from_the_base_item_code(self):
        catalog = build(folder_reader(self.excel))
        by_name = {entry['name']: entry for entry in catalog['uniques']}
        self.assertEqual(by_name['The Gnasher']['type'], 'axe')
        self.assertEqual(by_name['The Stone of Jordan']['type'], 'ring')

    def test_sets_keep_their_set_name(self):
        catalog = build(folder_reader(self.excel))
        self.assertEqual(catalog['sets'][0]['setName'], "Civerb's Vestments")

    def test_runewords_need_complete_and_keep_their_runes(self):
        catalog = build(folder_reader(self.excel))
        names = [entry['name'] for entry in catalog['runewords']]
        self.assertNotIn('Unfinished', names)
        beast = next(entry for entry in catalog['runewords'] if entry['name'] == 'Beast')
        self.assertEqual(beast['runes'], ['r30', 'r03', 'r22'])

    def test_slots_number_each_category_from_zero(self):
        catalog = build(folder_reader(self.excel))
        for category, entries in catalog.items():
            self.assertEqual([entry['slot'] for entry in entries], list(range(len(entries))))

    def test_prepare_writes_the_file_and_reports_counts(self):
        out = Path(self.temporary.name) / 'out'
        summary = prepare(self.game, out)
        self.assertEqual(summary['uniques'], 3)
        text = (out / 'collection.tsv').read_text(encoding='utf-8').splitlines()
        start = next(index for index, line in enumerate(text) if line.startswith('# category'))
        header = text[start][2:].split('\t')
        self.assertEqual(header, ['category', 'code', 'recordId', 'name', 'level', 'type', 'extra'])
        rows = [dict(zip(header, line.split('\t'))) for line in text[start + 1:] if line.strip()]
        self.assertEqual(len([row for row in rows if row['category'] == 'uniques']), 3)
        self.assertEqual(len([row for row in rows if row['category'] == 'runewords']), 2)
        # The slot is the entry's order inside its category: uniques start at slot 0.
        self.assertEqual(rows[0]['category'], 'uniques')
        # A set carries its set name in the extra column.
        sets = [row for row in rows if row['category'] == 'sets']
        self.assertEqual(sets[0]['extra'], "Civerb's Vestments")


if __name__ == '__main__':
    unittest.main()
