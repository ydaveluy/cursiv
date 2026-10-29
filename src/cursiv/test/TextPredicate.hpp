// The factories that fill a predicate arm (`messageMatches`, `hoverMatches`):
// what a `/regex/` literal says elsewhere, said as a value a caller can write
// inline.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace cursiv::test {

[[nodiscard]] std::function<bool(std::string_view)> contains(
    std::string substring);

// ECMAScript grammar, searched anywhere in the text; the pattern is compiled
// once, at the factory call.
[[nodiscard]] std::function<bool(std::string_view)> matches(
    const std::string& ecmaPattern);

}  // namespace cursiv::test
