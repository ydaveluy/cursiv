// What the build states about a language beyond its grammar.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv {

struct LanguageIdentity {
  std::string id;
  std::vector<std::string> extensions;
  std::vector<std::string> fileNames;
};

// A CMake list arrives as one string; empty means no entry, not one empty one.
inline std::vector<std::string> split(std::string_view text) {
  std::vector<std::string> out;
  while (!text.empty()) {
    const std::size_t at = text.find(',');
    out.emplace_back(text.substr(0, at));
    if (at == std::string_view::npos) break;
    text.remove_prefix(at + 1);
  }
  return out;
}

}  // namespace cursiv
