// Where every check of this module reports: the helpers compare, the hook
// decides what a mismatch means, so they work under any test framework.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace cursiv::test {

// The two sides as printed text, and the message.
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
