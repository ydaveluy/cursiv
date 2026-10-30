#!/usr/bin/env python3
"""Write src/cursiv/core/grammar/unicode/UnicodeRanges.hpp from Python's
unicodedata: the code point ranges of every general category, their majors,
the identifier properties `str.isidentifier` answers (XID_Start,
XID_Continue), White_Space, and Alphabetic, read from the Unicode Character
Database of the same version, indexed by one enumeration; and
UnicodeLower.hpp: what `str.lower` maps each scalar to, and the casing
properties final sigma reads."""
import os, sys, unicodedata, urllib.request

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
# signs that spell a letter's sound, from the Unicode version unicodedata has.
version = unicodedata.unidata_version
url = f'https://www.unicode.org/Public/{version}/ucd/DerivedCoreProperties.txt'
derived = urllib.request.urlopen(url).read().decode('utf-8')
assert derived.startswith(f'# DerivedCoreProperties-{version}.txt'), url
alphabetic = []
for line in derived.splitlines():
    fields = [f.strip() for f in line.split('#')[0].split(';')]
    if len(fields) == 2 and fields[1] == 'Alphabetic':
        lo, _, hi = fields[0].partition('..')
        alphabetic.extend(range(int(lo, 16), int(hi or lo, 16) + 1))
props['Alphabetic'] = ranges(sorted(alphabetic))

here = os.path.dirname(__file__)
unicode_dir = os.path.join(here, '..', 'src', 'cursiv', 'core', 'grammar', 'unicode')
out = [
    '// The Unicode properties a class of code points may name, written by',
    '// tools/gen_unicode_ranges.py from Python\'s unicodedata and the Unicode',
    '// Character Database: never edited.',
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
    '// Character Database: never edited.',
    '#pragma once', '', '#include <span>', '',
    '#include "cursiv/core/grammar/unicode/UnicodeProperty.hpp"',
    '#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"', '',
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
mid = {0x27, 0x2E, 0x3A, 0xB7, 0x387, 0x55F, 0x5F4, 0x2018, 0x2019, 0x2024,
       0x2027, 0xFE13, 0xFE52, 0xFE55, 0xFF07, 0xFF0E, 0xFF1A}
ignorable = ranges(cp for cp in range(MAX + 1)
                   if cp in mid or unicodedata.category(chr(cp))
                   in ('Mn', 'Me', 'Cf', 'Lm', 'Sk'))
out = [
    '// What each scalar lowers to, and the casing properties final sigma reads,',
    '// written by tools/gen_unicode_ranges.py from Python\'s unicodedata: never',
    '// edited.',
    '#pragma once', '', '#include <cstdint>', '',
    '#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"', '',
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
