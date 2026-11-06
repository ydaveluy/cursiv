#include "cursiv/ExpectFunction.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/ExpectationError.hpp"

namespace cursiv::test {

namespace {

// The installed hook, one per process: a harness sets it once, before any
// check runs.
ExpectFunction& hook() {
  static ExpectFunction installed;
  return installed;
}

// Each item quoted, its quotes and backslashes escaped: `["a, b"]` is not
// `["a", "b"]`.
std::string listed(std::span<const std::string> items) {
  std::string out = "[";
  for (std::size_t at = 0; at < items.size(); ++at) {
    if (at != 0) out += ", ";
    out += '"';
    for (const char c : items[at]) {
      if (c == '"' || c == '\\') out += '\\';
      out += c;
    }
    out += '"';
  }
  out += ']';
  return out;
}

}  // namespace

void setExpectFunction(ExpectFunction expectFunction) {
  hook() = std::move(expectFunction);
}

void expectedFunction(std::string_view actual, std::string_view expected,
                      std::string_view message) {
  if (hook()) {
    hook()(actual, expected, message);
    return;
  }
  if (actual == expected) return;
  std::string sentence(message);
  if (!sentence.empty()) sentence += ": ";
  sentence += "expected '";
  sentence += expected;
  sentence += "' but received '";
  sentence += actual;
  sentence += '\'';
  throw ExpectationError(sentence);
}

void detail::expectedItems(std::span<const std::string> actual,
                           std::span<const std::string> expected,
                           std::string_view lengthMessage) {
  if (actual.size() != expected.size()) {
    expectedFunction(listed(actual), listed(expected), lengthMessage);
    return;
  }
  for (std::size_t at = 0; at < actual.size(); ++at)
    expectedFunction(actual[at], expected[at]);
}

}  // namespace cursiv::test
