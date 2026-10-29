// An engine's `Grammar`: one grammar's answers about its records and its
// tokens, from the metadata a generator writes, a record's row read in the
// stamp layout of the table that wrote the tree. The same class for every
// grammar and every engine.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarMetaData.hpp"
#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct StampedGrammar final : Grammar {
  StampedGrammar(const GrammarMetaData& meta, StampLayout stamp) noexcept
      : _f(&meta), _stamp(stamp) {}

  bool recordWrites(const CstNode& n, std::string_view f) const noexcept final {
    const ElementId e = _stamp.row(n);
    return holds(e) && !f.empty() && answer(e).writes(f);
  }
  bool recordFills(const CstNode& n, std::string_view f) const noexcept final {
    const ElementId e = _stamp.row(n);
    return holds(e) && !f.empty() && answer(e).fills(f);
  }
  std::string_view recordFeature(const CstNode& n) const noexcept final {
    return rowFeature(_stamp.row(n));
  }
  std::string_view recordRule(const CstNode& n) const noexcept final {
    const ElementId e = _stamp.row(n);
    return holds(e) ? _f->names[answer(e).rule] : std::string_view{};
  }

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
      std::uint16_t t) const noexcept final {
    return t < _f->terminalCount ? _f->terminals[t].spellings
                                 : std::span<const std::string_view>{};
  }
  std::string_view terminalName(std::uint16_t t) const noexcept final {
    return t < _f->terminalCount ? _f->terminals[t].name : std::string_view{};
  }
  std::string_view terminalDoc(std::uint16_t t) const noexcept final {
    return t < _f->terminalCount ? _f->terminals[t].doc : std::string_view{};
  }
  // A linear scan: a hover is a keystroke.
  std::string_view spellingDoc(std::string_view text) const noexcept final {
    if (text.empty()) return {};
    for (std::uint16_t t = 0; t < _f->terminalCount; ++t) {
      const auto& term = _f->terminals[t];
      if (term.doc.empty()) continue;
      for (const std::string_view spelling : term.spellings)
        if (term.folded ? folded_equal(spelling, text) : spelling == text)
          return term.doc;
    }
    return {};
  }

  // The words a caret types for a terminal that spells none, to ask of it what
  // only a word can answer.
  static constexpr std::array<std::string_view, 9> samples{
      "a", "name", "0", "1", "_", "\"\"", "''", "\n", "\r\n"};

  // The text from `from` to the caret `at`, the spelling, then the real text
  // past `resume` in growing slices, until `walk` halts inside one or the
  // document ends: an end right after the spelling accepts an unfinished
  // prefix.
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

  // Whether the trivia token `term` that `run` holds still ends where `run`
  // does with a word typed behind it: a blank run does, a line comment does
  // not. The word is the first spelling of `terms`, else sample, that `lex`
  // reads whole as no trivia.
  static bool ends_before_word(std::string& probe, std::string_view run,
                               std::uint16_t term,
                               std::span<const TermInfo> terms,
                               const char* names, auto&& lex) {
    // -1 where `word` is no word here.
    const auto ends = [&](std::string_view word) {
      const auto read = lex(word);
      if (read.end != word.size() || read.term >= terms.size() ||
          terms[read.term].trivia)
        return -1;
      probe.assign(run);
      probe.append(word);
      const auto s = lex(std::string_view(probe));
      return s.term == term && s.end == run.size() ? 1 : 0;
    };
    for (const TermInfo& t : terms)
      if (t.spelled)
        if (const int e = ends({names + t.textOff, t.textLen}); e >= 0)
          return e != 0;
    for (const std::string_view sample : samples)
      if (const int e = ends(sample); e >= 0) return e != 0;
    return true;
  }

  StampLayout stamp() const noexcept { return _stamp; }

 private:
  bool holds(ElementId e) const noexcept { return e < _f->recordCount; }
  const GrammarMetaData::Answer& answer(ElementId e) const noexcept {
    return _f->answers[_f->recordAnswers[e]];
  }

  const GrammarMetaData* _f;
  StampLayout _stamp;
};

}  // namespace cursiv
