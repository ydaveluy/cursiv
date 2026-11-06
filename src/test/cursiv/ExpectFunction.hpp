// Where every check in this module is reported: the helpers compare, the
// hook decides what a mismatch means -- which is what lets them sit under
// any test framework instead of one.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace cursiv::test {

// The two sides already rendered, plus the sentence: a C++ module has no
// `unknown` to hand over, so it hands over what it would have printed.
using ExpectFunction =
    std::function<void(std::string_view actual, std::string_view expected,
                       std::string_view message)>;

// An empty function restores the default, which throws `ExpectationError`.
void setExpectFunction(ExpectFunction expectFunction);

void expectedFunction(std::string_view actual, std::string_view expected,
                      std::string_view message = {});

namespace detail {

// Two lists, item by item. Lists of different lengths always fail, with
// `lengthMessage`: each side is shown with its items quoted, so two different
// lists never read the same.
void expectedItems(std::span<const std::string> actual,
                   std::span<const std::string> expected,
                   std::string_view lengthMessage);

}  // namespace detail

}  // namespace cursiv::test
