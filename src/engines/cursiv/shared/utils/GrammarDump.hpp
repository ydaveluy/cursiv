#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The dump, folded for review: long rules broken at their top-level
// alternatives and filled to a column. It folds both the served grammar source
// and `GRAMMAR.txt`.

namespace cursiv::engine {

// The printed grammar: each rule's span of `text` and the rules it uses, by
// index.
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

}  // namespace cursiv::engine
