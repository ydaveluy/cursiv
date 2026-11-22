#pragma once

// A category is an object whose address is its identity: `inline constexpr`
// gives it one address program-wide. A check is filed under one when it is
// registered, `kFast` by default. The description is for display only.
namespace cursiv {

struct ValidationCategory {
  const char* description = nullptr;
};

// `kFast` is what a check is unless it says otherwise, cheap enough for every
// keystroke; `kSlow` is one a build may skip, run on save or on demand.
inline constexpr ValidationCategory kFast{"fast"};
inline constexpr ValidationCategory kSlow{"slow"};
// The runtime's own half: parse diagnostics and linking errors. Name it to run
// that half alone, or leave it out to skip it. No language check may use it.
inline constexpr ValidationCategory kBuiltIn{"built-in"};

}  // namespace cursiv
