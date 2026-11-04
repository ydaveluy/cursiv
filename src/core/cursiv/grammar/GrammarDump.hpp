#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The dump, folded for review: long rules broken at their top-level
// alternatives and filled to a column. It reads the text and nothing of the
// model, which is why it sits beside the source that serves it -- the runtime
// and `GRAMMAR.txt` must fold identically.

namespace cursiv {

// Produced while walking the grammar, before emitting runtime source. Ranks
// identify rules independently of their printed names or body spelling.
struct GrammarDump {
  struct Rule {
    std::string name;
    std::size_t begin = 0;
    std::size_t size = 0;
    std::vector<std::size_t> dependencies;
  };
  std::string text;
  std::vector<Rule> rules;
};

[[nodiscard]] std::string dump_wrapped(std::string_view text,
                                       std::size_t width = 92);

}  // namespace cursiv
