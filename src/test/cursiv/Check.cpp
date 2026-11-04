#include "cursiv/Check.hpp"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>

#include "cursiv/ExpectFunction.hpp"

namespace cursiv::test {

void count_expectations() {
  cursiv::test::setExpectFunction([](std::string_view actual,
                                     std::string_view expected,
                                     std::string_view message) {
    if (actual == expected) return;
    if (!message.empty())
      std::printf("FAIL %.*s\n", static_cast<int>(message.size()),
                  message.data());
    std::printf("FAIL expected '%.*s' but received '%.*s'\n",
                static_cast<int>(expected.size()), expected.data(),
                static_cast<int>(actual.size()), actual.data());
    ++failures;
  });
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

}  // namespace cursiv::test
