// An engine's `Grammar`: the metadata's answers, a record's row read in the
// stamp layout of the table that wrote the tree.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/DefaultGrammar.hpp"
#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/GrammarMetaData.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

struct StampedGrammar final : DefaultGrammar<StampedGrammar> {
  StampedGrammar(const GrammarMetaData& meta, StampLayout stamp) noexcept
      : DefaultGrammar(meta), _stamp(stamp) {}

  // The words a caret types for a terminal that spells none, to ask of it what
  // only a word can answer.
  static constexpr std::array<std::string_view, 9> samples{
      "a", "name", "0", "1", "_", "\"\"", "''", "\n", "\r\n"};

  // The text from `from` to the caret `at`, the spelling, then the real text
  // past `resume` in growing slices, until `walk` halts inside one or the
  // document ends: an end right after the spelling accepts an unfinished prefix.
  static auto spliced(std::string& splice, std::string_view text,
                      TextOffset from, TextOffset at, TextOffset resume,
                      std::string_view spelling, auto&& walk) {
    for (std::size_t tail = 16;; tail *= 2) {
      const std::size_t have = std::min(text.size() - resume, tail);
      splice.assign(text.data() + from, at - from);
      splice.append(spelling);
      splice.append(text.data() + resume, have);
      TextOffset halt = 0;
      auto answer = walk(halt);
      if (halt < splice.size() || resume + have >= text.size()) return answer;
    }
  }

  StampLayout stamp() const noexcept { return _stamp; }
  ElementId rowOf(const CstNode& n) const noexcept { return _stamp.row(n); }

 private:
  StampLayout _stamp;
};

}  // namespace cursiv
