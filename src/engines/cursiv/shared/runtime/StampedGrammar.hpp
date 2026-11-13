// An engine's `Grammar`: one grammar's answers about its records and tokens,
// from the metadata a generator writes. The same class for every grammar and
// every engine.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/GrammarMetaData.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstLeaf.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

struct StampedGrammar final : Grammar {
  StampedGrammar(const detail::GrammarMetaData& meta,
                 StampLayout stamp) noexcept
      : _f(&meta), _stamp(stamp) {}

  bool recordWrites(const CstNode& n,
                    std::string_view f) const noexcept override;
  bool recordFills(const CstNode& n,
                   std::string_view f) const noexcept override;
  std::string_view recordFeature(const CstNode& n) const noexcept override;
  std::string_view recordRule(const CstNode& n) const noexcept override;

  bool recordIsDataRule(const CstNode& n) const noexcept override;

  // The same answer from a row and not a record: a continuation has no
  // record yet -- the table names the site a shift would write in.
  std::string_view rowFeature(ElementId e) const noexcept {
    return holds(e) ? _f->names[answer(e).feature] : std::string_view{};
  }
  std::uint8_t rowNarrow(ElementId e) const noexcept {
    return holds(e) && _f->narrows != nullptr ? _f->narrows[e] : kNoNarrow;
  }
  const AstTypeTag* rowType(ElementId e) const noexcept {
    return holds(e) ? _f->ast->tag_of(answer(e).type) : nullptr;
  }

  std::span<const std::string_view> terminalSpellings(
      std::uint16_t t) const noexcept override;
  std::string_view terminalName(std::uint16_t t) const noexcept override;
  std::string_view terminalDoc(std::uint16_t t) const noexcept override;
  // A linear scan is enough: a hover runs once per keystroke.
  std::string_view spellingDoc(std::string_view text) const noexcept override;
  // A linear scan: a lookup asks it once, and only of a word it met written in
  // another case.
  bool spellingFolds(std::string_view keyword) const noexcept override;
  std::string_view recordDoc(const CstNode& n) const noexcept override;

  // Whether a caret at `at`, its candidate replacing the text to `resume`, has
  // an answer in the tree. A caret inside a token, a comment or dropped text
  // is asked at that leaf's start: the answer replaces the leaf.
  static bool caret_in(const Cst& cst, TextOffset& at, TextOffset& resume) {
    if (!cst.engine_stamped() || at > resume || resume > cst.text().size())
      return false;
    if (const NodeId after = cst.leaf_after(at);
        after < cst.size() && cst[after].begin < at) {
      const CstLeaf inside = cst.as_leaf(after);
      at = inside.begin;
      resume = std::max(resume, inside.end);
    }
    return true;
  }

  // The words a caret types for a terminal that spells none, to ask of it what
  // only a word can answer: a condition may turn on the case of a letter. The
  // last two only end a line.
  static constexpr std::array<std::string_view, 11> samples{
      "a", "name", "A", "Name", "0", "1", "_", "\"\"", "''", "\n", "\r\n"};
  static constexpr std::size_t kSampleWords = samples.size() - 2;

  // The text from `from` to the caret `at`, the spelling, then the real text
  // past `resume` in growing slices, until `walk` halts or the document ends.
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

  // Whether `run`, the text of token `term` up to a caret, is still all trivia
  // with a sample word typed behind it.
  static bool ends_before_word(std::string_view run, std::uint16_t term,
                               std::span<const TermInfo> terms, auto&& lex) {
    const auto trivia = [&](std::uint16_t t) {
      return t < terms.size() && terms[t].trivia;
    };
    std::string typed;
    for (std::size_t w = 0; w < kSampleWords; ++w) {
      if (trivia(lex(samples[w]).term)) continue;
      typed.assign(run).append(samples[w]);
      for (std::size_t from = 0; from < run.size();) {
        const auto s = lex(std::string_view(typed).substr(from));
        if (s.end > run.size() - from || (s.term != term && !trivia(s.term)))
          return false;
        from += s.end;
      }
    }
    return true;
  }

  StampLayout stamp() const noexcept { return _stamp; }

 private:
  bool holds(ElementId e) const noexcept { return e < _f->recordCount; }
  const detail::GrammarMetaData::Answer& answer(ElementId e) const noexcept {
    return _f->answers[_f->recordAnswers[e]];
  }

  const detail::GrammarMetaData* _f;
  StampLayout _stamp;
};

}  // namespace cursiv::engine
