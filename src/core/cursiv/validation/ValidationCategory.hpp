#pragma once

// A category is an object and its address is its identity, so comparing two is
// a pointer compare and no string ever travels. A check's return type is its
// category, `void` meaning `fast`.
//
// The return type rather than an attribute because reflection reads
// declarations, not attributes: there is no `attributes_of`, while a return
// type is reflectable and cannot be silently dropped by a compiler that does
// not know the spelling.
//
// `inline constexpr` gives the instance one address program-wide, which is what
// makes the address an identity. Two headers declaring their own `lint` declare
// two categories, on purpose.
//
// The description is display only, never identity, and may be null.
namespace cursiv {

struct ValidationCategory {
  const char* description = nullptr;
};

// `fast` is what a check is unless it says otherwise, cheap enough for every
// keystroke; `slow` is one a build may skip, run on save or on demand.
inline constexpr ValidationCategory fast{"fast"};
inline constexpr ValidationCategory slow{"slow"};
// The runtime's own half -- the parse diagnostics and the linking errors. A
// client names it to run that half alone, or leaves it out to skip it, a
// fast-only pass not having to re-walk every reference. Reserved: no language
// check may declare it, or the name would mean different things per language.
inline constexpr ValidationCategory built_in{"built-in"};

}  // namespace cursiv
