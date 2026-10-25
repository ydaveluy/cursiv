// The Unicode properties a class of code points may name, written by
// tools/gen_unicode_ranges.py from Python's unicodedata: never edited.
#pragma once

#include <string_view>

namespace cursiv::unicode {

// The general categories, their majors, and the identifier and space
// properties: a class of code points is a set of these.
enum class Property : unsigned {
  L,
  M,
  N,
  P,
  S,
  Z,
  C,
  Cc,
  Cf,
  Co,
  Ll,
  Lm,
  Lo,
  Lt,
  Lu,
  Mc,
  Me,
  Mn,
  Nd,
  Nl,
  No,
  Pc,
  Pd,
  Pe,
  Pf,
  Pi,
  Po,
  Ps,
  Sc,
  Sk,
  Sm,
  So,
  Zl,
  Zp,
  Zs,
  XID_Start,
  XID_Continue,
  White_Space,
  count
};

// For a dump alone: what a property is called.
inline constexpr std::string_view kNames[] = {
    "L",
    "M",
    "N",
    "P",
    "S",
    "Z",
    "C",
    "Cc",
    "Cf",
    "Co",
    "Ll",
    "Lm",
    "Lo",
    "Lt",
    "Lu",
    "Mc",
    "Me",
    "Mn",
    "Nd",
    "Nl",
    "No",
    "Pc",
    "Pd",
    "Pe",
    "Pf",
    "Pi",
    "Po",
    "Ps",
    "Sc",
    "Sk",
    "Sm",
    "So",
    "Zl",
    "Zp",
    "Zs",
    "XID_Start",
    "XID_Continue",
    "White_Space",
};

}  // namespace cursiv::unicode
