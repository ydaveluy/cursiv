#include "cursiv/test/TextPredicate.hpp"

#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace cursiv::test {

std::function<bool(std::string_view)> contains(std::string substring) {
  return [substring = std::move(substring)](std::string_view text) {
    return text.find(substring) != std::string_view::npos;
  };
}

std::function<bool(std::string_view)> matches(const std::string& ecmaPattern) {
  return [pattern = std::regex(ecmaPattern)](std::string_view text) {
    return std::regex_search(text.begin(), text.end(), pattern);
  };
}

}  // namespace cursiv::test
