#include "cursiv/test/ExpectFunction.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "cursiv/test/ExpectationError.hpp"

namespace cursiv::test {

namespace {

// The installed hook, one per process: a harness sets it once, before any
// check runs.
ExpectFunction& hook() {
  static ExpectFunction installed;
  return installed;
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

}  // namespace cursiv::test
