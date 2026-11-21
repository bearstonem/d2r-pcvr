#!/usr/bin/env python3
"""The Collection tooltip's stat lines, with roll ranges, from the game's own tables.

D2RLoader 1.3.1 gives no item text, but the catalog knows every unique, set item and
runeword: its rows in UniqueItems / SetItems / Runes list the properties with their
min and max. Each property is worded the way the game words it - Properties.txt
names the stat, ItemStatCost.txt its description string and priority, and the
English item-modifiers strings the text ('%+d to Strength'). A range reads
'+(5-10) to Strength'. Standard library only.
"""
import json
import re

CLASSES = {'ama': 'Amazon', 'sor': 'Sorceress', 'nec': 'Necromancer', 'pal': 'Paladin',
           'bar': 'Barbarian', 'dru': 'Druid', 'ass': 'Assassin'}
SKILL_TABS = ['Bow and Crossbow Skills/ama', 'Passive and Magic Skills/ama', 'Javelin and Spear Skills/ama',
              'Fire Skills/sor', 'Lightning Skills/sor', 'Cold Skills/sor',
              'Curses/nec', 'Poison and Bone Skills/nec', 'Summoning Skills/nec',
              'Combat Skills/pal', 'Offensive Auras/pal', 'Defensive Auras/pal',
              'Combat Skills/bar', 'Combat Masteries/bar', 'Warcries/bar',
              'Summoning Skills/dru', 'Shape Shifting Skills/dru', 'Elemental Skills/dru',
              'Traps/ass', 'Shadow Disciplines/ass', 'Martial Arts/ass']
# Damage pairs: one line 'Adds a-b ... damage' when an item has both halves.
PAIRS = {'dmg-min': ('dmg-max', 'strModMinDamageRange'), 'fire-min': ('fire-max', 'strModFireDamageRange'),
         'ltng-min': ('ltng-max', 'strModLightningDamageRange'), 'cold-min': ('cold-max', 'strModColdDamageRange'),
         'mag-min': ('mag-max', 'strModMagicDamageRange')}
RANGES = {'dmg-fire': 'strModFireDamageRange', 'dmg-cold': 'strModColdDamageRange',
          'dmg-ltng': 'strModLightningDamageRange', 'dmg-mag': 'strModMagicDamageRange',
          'dmg-norm': 'strModMinDamageRange'}
FIXED = {'dmg%': 'strModEnhancedDamage', 'res-all': 'strModAllResistances',
         'all-stats': 'Moditem2allattrib', 'allskills': 'ModStr3k'}
TRIGGERS = {'hit-skill', 'gethit-skill', 'att-skill', 'kill-skill', 'death-skill', 'levelup-skill'}
NUMBER = re.compile(r'%\+d|%d|%0')


def load_strings(json_texts):
    """Key -> English text from item-modifiers style JSON documents (first one wins)."""
    strings = {}
    for text in json_texts:
        for row in json.loads(text):
            if isinstance(row, dict) and row.get('Key') and row.get('Key') not in strings:
                strings[row['Key']] = row.get('enUS', '')
    return strings


_CACHE = {}


def _lookups(tables):
    found = _CACHE.get(id(tables))
    if found:
        return found
    props = {r.get('code', '').strip().lower(): r for r in tables.get('properties', [])}
    stats = {r.get('Stat', '').strip(): r for r in tables.get('itemstatcost', [])}
    skills_by_id, skills_by_name = {}, {}
    # The name the game shows (Firestorm), not the table's own (DiabWall): SkillDesc's 'str name'.
    shown = {r.get('skilldesc', '').strip(): r.get('str name', '').strip() for r in tables.get('skilldesc', [])}
    names = tables.get('skillnames', {})
    for r in tables.get('skills', []):
        key = shown.get(r.get('skilldesc', '').strip(), '')
        if names.get(key, '').strip():
            r = dict(r)
            r['shown'] = names[key].strip()
        name = r.get('skill', '').strip()
        sid = (r.get('*Id') or r.get('Id') or '').strip()
        if name:
            skills_by_name[name.lower()] = r
            if sid.isdigit():
                skills_by_id[int(sid)] = r
    found = (props, stats, skills_by_id, skills_by_name)
    _CACHE[id(tables)] = found
    return found


def _int(text, default=0):
    text = (text or '').strip()
    return int(text) if re.fullmatch(r'-?\d+', text) else default


def _skill(par, look):
    """(English skill name, its class code) from a name or a numeric id."""
    _, _, by_id, by_name = look
    par = (par or '').strip()
    row = by_id.get(int(par)) if par.isdigit() else by_name.get(par.lower())
    if row is None:
        return par, ''
    return row.get('shown') or row.get('skill', '').strip() or par, row.get('charclass', '').strip()


def _value(lo, hi, signed):
    """'5', '+5', '(5-10)', '+(5-10)'; a range with a negative bound reads '(a to b)'."""
    if hi is None or hi == lo:
        return ('+' if signed and lo >= 0 else '') + str(lo)
    if lo < 0 or hi < 0:
        return '(%d to %d)' % (lo, hi)
    return ('+' if signed else '') + '(%d-%d)' % (lo, hi)


def _fill(text, values):
    """Each placeholder in turn: %+d / %d / %0 take a number text (signed for %+d), %s / %1 a word."""
    values = list(values)

    def next_value(match):
        token = match.group(0)
        if not values:
            return ''
        value = values.pop(0)
        if isinstance(value, tuple):   # (lo, hi): a number, signed only for %+d
            return _value(value[0], value[1], token == '%+d')
        return str(value)
    text = re.sub(r'%\+d|%d|%0|%1|%s', next_value, text)
    return _clean(text)


def _clean(text):
    text = re.sub(r'ÿc.', '', text).replace('%%', '%')
    return re.sub(r'\s+', ' ', text).strip()


def _priority(stat_row):
    return _int(stat_row.get('descpriority', '')) if stat_row else 0


def _line(code, par, lo, hi, look, strings):
    """(priority, text) for one property, or None when the game shows nothing for it."""
    props, stats, _, _ = look
    prop = props.get(code)
    stat = stats.get(prop.get('stat1', '').strip(), {}) if prop else {}
    prio = _priority(stat)
    rng = (lo, hi)

    if code in FIXED:
        text = strings.get(FIXED[code])
        return (prio, _fill(text, [rng])) if text else None
    if code in RANGES:
        text = strings.get(RANGES[code])
        return (prio, _fill(text, [lo, hi if hi is not None else lo])) if text else None
    if code == 'dmg-pois':
        length = _int(par)
        text = strings.get('strModPoisonDamageRange')
        if not text or not length:
            return None
        lows = round(lo * length / 256)
        highs = round((hi if hi is not None else lo) * length / 256)
        return prio, _fill(text, [lows, highs, round(length / 25)])
    if code == 'aura':
        text = strings.get('ModitemAura')
        return (prio, _fill(text, [(lo, hi), _skill(par, look)[0]])) if text else None
    if code == 'charged':
        text = strings.get('ModStre10d')
        level = hi if hi is not None else lo
        return (prio, _fill(text, [level, _skill(par, look)[0], lo, lo])) if text else None
    if code in TRIGGERS or stat.get('descfunc', '').strip() == '15':
        text = strings.get(stat.get('descstrpos', '').strip())
        level = hi if hi is not None else lo
        return (prio, _fill(text, [lo, level, _skill(par, look)[0]])) if text else None
    if code in ('skill', 'oskill'):
        name, cls = _skill(par, look)
        text = '+%s to %s' % (_value(lo, hi, False), name)
        if code == 'skill' and cls in CLASSES:
            text += ' (%s Only)' % CLASSES[cls]
        return prio, text
    if code == 'skilltab':
        tab = _int(par, -1)
        if not 0 <= tab < len(SKILL_TABS):
            return None
        name, cls = SKILL_TABS[tab].split('/')
        return prio, '%s to %s (%s Only)' % (_value(lo, hi, True), name, CLASSES[cls])
    if code == 'randclassskill':   # Hellfire Torch: min..max is the class drawn, the bonus is three
        return prio, '+3 to (Random Class) Skill Levels'
    if code == 'sock':
        return prio, 'Socketed (%s)' % _value(lo, hi, False)
    if code == 'indestruct':
        return prio, strings.get('ModStre9s') or 'Indestructible'
    if code.endswith('/lvl'):
        text = strings.get(stat.get('descstrpos', '').strip())
        if not text:
            return None
        per = ('%.3f' % (_int(par) / 8)).rstrip('0').rstrip('.')
        text = NUMBER.sub('+(%s per Level)' % per, text, count=1)
        based = strings.get('increaseswithplaylevelX', '')
        return prio, _clean(text + (' ' + based if based and based not in text else ''))

    # The generic way: the stat's own description.
    func = _int(stat.get('descfunc', ''))
    if not prop or not stat or not func:
        return None
    if func in (5, 10):
        lo, hi = lo * 100 // 128, (hi * 100 // 128 if hi is not None else None)
    if func in (20, 21):
        lo, hi = -lo, (-hi if hi is not None else None)
    negative = (hi if hi is not None else lo) < 0
    key = stat.get('descstrneg', '').strip() if negative and stat.get('descstrneg', '').strip() else stat.get('descstrpos', '').strip()
    text = strings.get(key)
    if not text:
        return None
    word = strings.get(stat.get('descstr2', '').strip(), '')
    if NUMBER.search(text):
        filler = word or (_skill(par, look)[0] if par.strip() else '')
        text = _fill(text, [(lo, hi), filler])
    else:
        sign = func in (1, 4, 6, 8, 12, 15, 16, 19)
        value = _value(lo, hi, sign) + ('%' if func in (2, 4, 5, 7, 8, 10, 18, 22) else '')
        descval = stat.get('descval', '').strip()
        text = value + ' ' + text if descval == '1' else text + ' ' + value if descval == '2' else text
        if func in (6, 7, 8, 9, 10) and word and word not in text:
            text += ' ' + word
    return prio, _clean(text)


def stat_lines(row, kind, tables, strings):
    """The tooltip lines of one UniqueItems ('unique'), SetItems ('set') or Runes ('runeword') row."""
    if not strings:
        return []
    if kind == 'runeword':
        names, count = ('T1Code', 'T1Param', 'T1Min', 'T1Max'), 7
    else:
        names, count = ('prop', 'par', 'min', 'max'), 12 if kind == 'unique' else 9
    look = _lookups(tables)
    entries = []
    for i in range(1, count + 1):
        code = (row.get('%s%d' % (names[0], i)) or '').strip().lower()
        if code:
            lo = _int(row.get('%s%d' % (names[2], i)))
            hi_text = (row.get('%s%d' % (names[3], i)) or '').strip()
            hi = _int(hi_text) if hi_text else None
            entries.append([code, (row.get('%s%d' % (names[1], i)) or '').strip(), lo, hi])
    lines = []
    codes = {e[0]: e for e in entries}
    used = set()
    for order, (code, par, lo, hi) in enumerate(entries):
        if code in used:
            continue
        if code in PAIRS and PAIRS[code][0] in codes and strings.get(PAIRS[code][1]):
            other = codes[PAIRS[code][0]]
            used.add(other[0])
            _, stats, _, _ = _lookups(tables)
            text = _fill(strings[PAIRS[code][1]], [_value(lo, hi, False), _value(other[2], other[3], False)])
            prio = _priority(stats.get((_lookups(tables)[0].get(code) or {}).get('stat1', '').strip(), {}))
            lines.append((prio, order, text))
            continue
        made = _line(code, par, lo, hi, look, strings)
        if made and made[1]:
            lines.append((made[0], order, made[1]))
    lines.sort(key=lambda line: (-line[0], line[1]))
    return [line[2] for line in lines]


if __name__ == '__main__':
    tables = {
        'properties': [{'code': 'str', 'stat1': 'strength'}, {'code': 'dmg%', 'stat1': 'item_maxdamage_percent'},
                       {'code': 'fire-min', 'stat1': 'firemindam'}, {'code': 'fire-max', 'stat1': 'firemaxdam'}],
        'itemstatcost': [{'Stat': 'strength', 'descpriority': '67', 'descfunc': '1', 'descstrpos': 'ModStr1a'},
                         {'Stat': 'item_maxdamage_percent', 'descpriority': '129', 'descfunc': '4', 'descstrpos': 'ModStr2a'},
                         {'Stat': 'firemindam', 'descpriority': '102', 'descfunc': '19', 'descstrpos': 'ModStr1g'}],
        'skills': [],
    }
    strings = {'ModStr1a': '%+d to Strength', 'strModEnhancedDamage': '%+d%% Enhanced Damage',
               'strModFireDamageRange': 'Adds %d-%d fire damage'}
    row = {'prop1': 'str', 'min1': '5', 'max1': '10', 'prop2': 'dmg%', 'min2': '230', 'max2': '270',
           'prop3': 'fire-min', 'min3': '1', 'max3': '1', 'prop4': 'fire-max', 'min4': '12', 'max4': '12'}
    out = stat_lines(row, 'unique', tables, strings)
    assert out == ['+(230-270)% Enhanced Damage', 'Adds 1-12 fire damage', '+(5-10) to Strength'], out
    print('ok')
