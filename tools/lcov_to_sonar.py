"""Convert an `llvm-cov export -format=lcov` trace into SonarQube's generic
coverage XML. Usage: lcov_to_sonar.py <root> <in.lcov> <out.xml>

A line's branches are its BRDA records: llvm-cov already unifies template
instantiations there, so each source branch is counted once.
"""
import os
import sys
from xml.sax.saxutils import quoteattr

root, source, target = sys.argv[1:4]
files = {}
lines = None
for raw in open(source, encoding="utf-8"):
    tag, _, value = raw.rstrip("\n").partition(":")
    if tag == "SF":
        lines = files.setdefault(os.path.relpath(value, root), {})
    elif tag == "DA":
        number, hits = value.split(",")[:2]
        entry = lines.setdefault(int(number), [False, 0, 0])
        entry[0] = entry[0] or int(hits) > 0
    elif tag == "BRDA":
        number, _, _, taken = value.split(",")
        entry = lines.setdefault(int(number), [False, 0, 0])
        entry[1] += 1
        entry[2] += taken not in ("-", "0")

with open(target, "w", encoding="utf-8") as out:
    out.write('<coverage version="1">\n')
    for path in sorted(files):
        out.write(f"  <file path={quoteattr(path)}>\n")
        for number, (covered, branches, taken) in sorted(files[path].items()):
            extra = (f' branchesToCover="{branches}" coveredBranches="{taken}"'
                     if branches else "")
            out.write(f'    <lineToCover lineNumber="{number}" '
                      f'covered="{str(covered).lower()}"{extra}/>\n')
        out.write("  </file>\n")
    out.write("</coverage>\n")
