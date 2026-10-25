// One text drawn from a random grammar, kept as its tokens and the gaps
// between them: `gaps[i]` stands before token `i`, the last one after all.
#pragma once

#include <string>
#include <vector>

namespace cursiv {

struct RgramDocument {
  std::string text{};
  std::vector<char> toks{};
  std::vector<std::string> gaps{};
};

}  // namespace cursiv
