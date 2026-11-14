#pragma once

// A category is an object whose address is its identity: `inline constexpr`
// gives it one address program-wide. A check is filed under one when it is
// registered, `fast` by default. The description is for display only.
namespace cursiv {

struct ValidationCategory {
  const char* description = nullptr;
};

// `fast` is what a check is unless it says otherwise, cheap enough for every
// keystroke; `slow` is one a build may skip, run on save or on demand.
inline constexpr ValidationCategory fast{"fast"};
inline constexpr ValidationCategory slow{"slow"};
// The runtime's own half: parse diagnostics and linking errors. Name it to run
// that half alone, or leave it out to skip it. No language check may use it.
inline constexpr ValidationCategory built_in{"built-in"};

}  // namespace cursiv
