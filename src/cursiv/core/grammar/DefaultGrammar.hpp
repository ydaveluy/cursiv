// One grammar's answers about its records and its tokens, through the erased
// door, from the metadata a generator writes: the same class for every grammar
// and every engine. An engine supplies only how its stamp names a record's
// row, as `Engine::rowOf(const CstNode&)`.
#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/CaseFold.hpp"

namespace cursiv {

template <class Engine>
struct DefaultGrammar : Grammar {
  explicit DefaultGrammar(const GrammarMetaData& meta) noexcept : _f(&meta) {}

  bool holds(ElementId e) const noexcept { return e < _f->recordCount; }

  bool recordWrites(const CstNode& n,
                    std::string_view f) const noexcept final {
    const ElementId e = row(n);
    return holds(e) && !f.empty() && _f->writes[e](f);
  }
  bool recordFills(const CstNode& n,
                   std::string_view f) const noexcept final {
    const ElementId e = row(n);
    return holds(e) && !f.empty() && _f->fills[e](f);
  }
  std::string_view recordFeature(const CstNode& n) const noexcept final {
    return rowFeature(row(n));
  }
  std::string_view recordRule(const CstNode& n) const noexcept final {
    const ElementId e = row(n);
    return holds(e) ? _f->rules[e]() : std::string_view{};
  }

  // The same answer from a row and not a record: a continuation has no
  // record yet -- the table names the site a shift would write in.
  std::string_view rowFeature(ElementId e) const noexcept {
    return holds(e) ? _f->features[e]() : std::string_view{};
  }
  std::uint8_t rowNarrow(ElementId e) const noexcept {
    return holds(e) && _f->narrows != nullptr ? _f->narrows[e] : kNoNarrow;
  }
  const AstTypeTag* rowType(ElementId e) const noexcept {
    return holds(e) ? _f->types[e] : nullptr;
  }

  std::string_view terminalText(std::uint16_t t) const noexcept final {
    return t < _f->terminalCount ? _f->terminals[t].text : std::string_view{};
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
      if (!term.doc.empty() &&
          (term.folded ? folded_equal(term.text, text) : term.text == text))
        return term.doc;
    }
    return {};
  }

 private:
  ElementId row(const CstNode& n) const noexcept {
    return static_cast<const Engine&>(*this).rowOf(n);
  }

  const GrammarMetaData* _f;
};

}  // namespace cursiv
