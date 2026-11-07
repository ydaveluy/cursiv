#!/usr/bin/env python3
"""Write src/core/cursiv/grammar/unicode/UnicodeRanges.hpp from Python's
unicodedata: the code point ranges of every general category, their majors,
the identifier properties `str.isidentifier` answers (XID_Start,
XID_Continue), White_Space, and Alphabetic, read from the Unicode Character
Database of the same version, indexed by one enumeration; and
UnicodeLower.hpp: what `str.lower` maps each scalar to, and the casing
properties final sigma reads; and UnicodeFold.hpp: what each scalar folds to
under simple case folding."""
import os, sys, unicodedata, urllib.request

# The one Unicode version of every table: unicodedata must have it, and the
# files read from the Unicode Character Database are of it. A new version is
# a change of this line, and of the Python that runs the script.
VERSION = '16.0.0'
assert unicodedata.unidata_version == VERSION, (
    f'unicodedata has Unicode {unicodedata.unidata_version}, not {VERSION}')

def ucd(path):
    url = f'https://www.unicode.org/Public/{VERSION}/ucd/{path}'
    text = urllib.request.urlopen(url).read().decode('utf-8')
    name = os.path.basename(path).removesuffix('.txt')
    assert text.startswith(f'# {name}-{VERSION}.txt'), url
    return text

# The code points a UCD property file gives one of `values`.
def listed(text, values):
    out = []
    for line in text.splitlines():
        fields = [f.strip() for f in line.split('#')[0].split(';')]
        if len(fields) == 2 and fields[1] in values:
            lo, _, hi = fields[0].partition('..')
            out.extend(range(int(lo, 16), int(hi or lo, 16) + 1))
    return sorted(out)

MAX = 0x10FFFF
cats = {}
for cp in range(MAX + 1):
    cats.setdefault(unicodedata.category(chr(cp)), []).append(cp)

def ranges(cps):
    out = []
    for cp in cps:
        if out and out[-1][1] + 1 == cp:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return out

props = {}
for major in 'LMNPSZC':
    cps = sorted(cp for name, l in cats.items()
                 if name[0] == major and name != 'Cs' for cp in l)
    props[major] = ranges(cps)
for name, cps in sorted(cats.items()):
    if name in ('Cn', 'Cs'):
        continue
    props[name] = ranges(cps)
props['XID_Start'] = ranges(cp for cp in range(MAX + 1)
                            if chr(cp).isidentifier() and cp != 0x5F)
props['XID_Continue'] = ranges(cp for cp in range(MAX + 1)
                               if ('a' + chr(cp)).isidentifier())
# str.isspace also holds the separators U+001C-U+001F, which White_Space does not.
props['White_Space'] = ranges(cp for cp in range(MAX + 1)
                              if chr(cp).isspace() and not 0x1C <= cp <= 0x1F)
# Alphabetic, which unicodedata lacks: the letters, letter numbers and the
# signs that spell a letter's sound.
props['Alphabetic'] = ranges(listed(ucd('DerivedCoreProperties.txt'),
                                    {'Alphabetic'}))

here = os.path.dirname(__file__)
unicode_dir = os.path.join(here, '..', 'src', 'core', 'cursiv', 'grammar',
                           'unicode')
# Beside the hand-written reader of the tables, or the layout has moved.
assert os.path.isfile(os.path.join(unicode_dir, 'UnicodeRange.hpp')), unicode_dir
out = [
    '// The Unicode properties a class of code points may name, written by',
    '// tools/gen_unicode_ranges.py from Python\'s unicodedata and the Unicode',
    f'// Character Database, Unicode {VERSION}: never edited.',
    '#pragma once', '', '#include <string_view>', '',
    'namespace cursiv::unicode {', '',
    '// The general categories, their majors, and the identifier, space and',
    '// alphabetic properties: a class of code points is a set of these.',
    'enum class Property : unsigned {',
]
for name in props:
    out.append(f'  {name},')
out += ['  count', '};', '', '// For a dump alone: what a property is called.',
        'inline constexpr std::string_view kNames[] = {']
for name in props:
    out.append(f'    "{name}",')
out += ['};', '', '}  // namespace cursiv::unicode', '']
open(os.path.join(unicode_dir, 'UnicodeProperty.hpp'), 'w').write('\n'.join(out))

out = [
    '// Code point ranges by Unicode property, written by',
    '// tools/gen_unicode_ranges.py from Python\'s unicodedata and the Unicode',
    f'// Character Database, Unicode {VERSION}: never edited.',
    '#pragma once', '', '#include <span>', '',
    '#include "cursiv/grammar/unicode/UnicodeProperty.hpp"',
    '#include "cursiv/grammar/unicode/UnicodeRange.hpp"', '',
    'namespace cursiv::unicode {', '',
]
for name, rs in props.items():
    body = ','.join(f'{{{lo:#x},{hi:#x}}}' for lo, hi in rs)
    out.append(f'inline constexpr Range k{name}[] = {{{body}}};')
out += ['', 'inline constexpr std::span<const Range> kTables[] = {']
for name in props:
    out.append(f'    k{name},')
out += ['};', '', '}  // namespace cursiv::unicode', '']
path = os.path.join(unicode_dir, 'UnicodeRanges.hpp')
open(path, 'w').write('\n'.join(out))
print(path, sum(len(r) for r in props.values()), 'ranges')

# Lowercase: runs of scalars that lower by one offset, every `stride` apart.
# U+0130 is the one scalar lowering to two, written by the reader.
multi = [cp for cp in range(MAX + 1) if len(chr(cp).lower()) > 1]
assert multi == [0x130], multi
runs = []
for cp in range(MAX + 1):
    if 0xD800 <= cp <= 0xDFFF or cp == 0x130:
        continue
    lo = ord(chr(cp).lower())
    if lo == cp:
        continue
    if runs:
        first, last, delta, stride = runs[-1]
        if delta == lo - cp and cp - last in (1, 2) and stride in (0, cp - last):
            runs[-1] = [first, cp, delta, cp - last]
            continue
    runs.append([cp, cp, lo - cp, 0])
cased = ranges(cp for cp in range(MAX + 1)
               if unicodedata.category(chr(cp)) in ('Lu', 'Ll', 'Lt')
               or chr(cp).lower() != chr(cp) or chr(cp).upper() != chr(cp))
# Word_Break MidLetter, MidNumLet and Single_Quote, which unicodedata lacks.
mid = set(listed(ucd('auxiliary/WordBreakProperty.txt'),
                 {'MidLetter', 'MidNumLet', 'Single_Quote'}))
ignorable = ranges(cp for cp in range(MAX + 1)
                   if cp in mid or unicodedata.category(chr(cp))
                   in ('Mn', 'Me', 'Cf', 'Lm', 'Sk'))
out = [
    '// What each scalar lowers to, and the casing properties final sigma reads,',
    '// written by tools/gen_unicode_ranges.py from Python\'s unicodedata and the',
    f'// Unicode Character Database, Unicode {VERSION}: never edited.',
    '#pragma once', '', '#include <cstdint>', '',
    '#include "cursiv/grammar/unicode/UnicodeRange.hpp"', '',
    'namespace cursiv::unicode {', '',
    '// From `first` to `last`, every `stride`-th scalar lowers by `delta`.',
    'struct LowerRun {', '  char32_t first;', '  char32_t last;',
    '  std::int32_t delta;', '  std::uint8_t stride;', '};', '',
]
body = ','.join(f'{{{f:#x},{l:#x},{d},{st or 1}}}' for f, l, d, st in runs)
out.append(f'inline constexpr LowerRun kLower[] = {{{body}}};')
for name, rs in (('Cased', cased), ('CaseIgnorable', ignorable)):
    body = ','.join(f'{{{lo:#x},{hi:#x}}}' for lo, hi in rs)
    out.append(f'inline constexpr Range k{name}[] = {{{body}}};')
out += ['', '}  // namespace cursiv::unicode', '']
path = os.path.join(unicode_dir, 'UnicodeLower.hpp')
open(path, 'w').write('\n'.join(out))
print(path, len(runs), 'runs')

# Simple case folding, the statuses C and S of CaseFolding.txt: what each
# scalar folds to. A scalar folds only to a case as long in UTF-8 as itself,
# so a name and its folding take as many bytes: `ẞ`, the Kelvin and Ohm
# signs and a few more keep themselves, and an ASCII letter matches its two
# cases and nothing else.
utf8_len = lambda cp: len(chr(cp).encode())
fold = {}
kept = []
for line in ucd('CaseFolding.txt').splitlines():
    fields = [f.strip() for f in line.split('#')[0].split(';')]
    if len(fields) < 3 or fields[1] not in ('C', 'S'):
        continue
    cp, to = int(fields[0], 16), int(fields[2], 16)
    if fields[1] == 'C':
        assert chr(cp).casefold() == chr(to), hex(cp)
    if utf8_len(cp) != utf8_len(to):
        kept.append(cp)
        continue
    fold[cp] = to
assert all(utf8_len(cp) == utf8_len(to) for cp, to in fold.items())
# Folding is stable: what a scalar folds to folds to itself.
assert all(fold.get(to, to) == to for to in fold.values())
print('kept as they are:', ' '.join(f'U+{cp:04X}' for cp in kept))
# Two levels: the block of each run of 64 scalars, and in each block an index
# into the deltas. Block 0 and delta 0 are the identity.
end = max(fold) + 1
deltas = [0]
blocks = [(0,) * 64]
pages = []
for page in range((end + 63) // 64):
    row = []
    for cp in range(page * 64, page * 64 + 64):
        d = fold.get(cp, cp) - cp
        if d not in deltas:
            deltas.append(d)
        row.append(deltas.index(d))
    row = tuple(row)
    if row not in blocks:
        blocks.append(row)
    pages.append(blocks.index(row))
assert len(blocks) <= 256 and len(deltas) <= 256
out = [
    '// What each scalar folds to under simple case folding, written by',
    '// tools/gen_unicode_ranges.py from the Unicode Character Database,',
    f'// Unicode {VERSION}: never edited.',
    '#pragma once', '', '#include <cstdint>', '',
    'namespace cursiv::unicode {', '',
    '// The statuses C and S of CaseFolding.txt, except that a scalar folds only',
    '// to a case as long in UTF-8. A scalar from `kFoldEnd` folds to itself;',
    '// below, it folds to itself plus',
    '// `kFoldDelta[kFoldBlock[kFoldPage[cp >> 6]][cp & 63]]`.',
    f'inline constexpr char32_t kFoldEnd = {end:#x};',
    'inline constexpr std::uint8_t kFoldPage[] = {' +
    ','.join(str(b) for b in pages) + '};',
    'inline constexpr std::uint8_t kFoldBlock[][64] = {' +
    ','.join('{' + ','.join(str(i) for i in b) + '}' for b in blocks) + '};',
    'inline constexpr std::int32_t kFoldDelta[] = {' +
    ','.join(str(d) for d in deltas) + '};',
    '', '}  // namespace cursiv::unicode', '']
path = os.path.join(unicode_dir, 'UnicodeFold.hpp')
open(path, 'w').write('\n'.join(out))
print(path, len(fold), 'scalars', len(blocks), 'blocks',
      len(pages) + 64 * len(blocks) + 4 * len(deltas), 'bytes')
