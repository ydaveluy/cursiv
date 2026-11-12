// The answers a `Grammar` gives through its virtual interface, compiled once
// for every grammar and every engine.
#include "cursiv/shared/runtime/StampedGrammar.hpp"

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/utils/CaseFold.hpp"

namespace cursiv::engine {

bool StampedGrammar::recordWrites(const CstNode& n,
                                  std::string_view f) const noexcept {
  const ElementId e = _stamp.row(n);
  return holds(e) && !f.empty() && answer(e).writes(f);
}

bool StampedGrammar::recordFills(const CstNode& n,
                                 std::string_view f) const noexcept {
  const ElementId e = _stamp.row(n);
  return holds(e) && !f.empty() && answer(e).fills(f);
}

std::string_view StampedGrammar::recordFeature(
    const CstNode& n) const noexcept {
  return rowFeature(_stamp.row(n));
}

std::string_view StampedGrammar::recordRule(const CstNode& n) const noexcept {
  const ElementId e = _stamp.row(n);
  return holds(e) ? _f->names[answer(e).rule] : std::string_view{};
}

bool StampedGrammar::recordIsDataRule(const CstNode& n) const noexcept {
  const ElementId e = _stamp.row(n);
  return holds(e) && answer(e).dataRule;
}

std::span<const std::string_view> StampedGrammar::terminalSpellings(
    std::uint16_t t) const noexcept {
  return t < _f->terminalCount ? _f->terminals[t].spellings
                               : std::span<const std::string_view>{};
}

std::string_view StampedGrammar::terminalName(std::uint16_t t) const noexcept {
  return t < _f->terminalCount ? _f->terminals[t].name : std::string_view{};
}

std::string_view StampedGrammar::terminalDoc(std::uint16_t t) const noexcept {
  return t < _f->terminalCount ? _f->terminals[t].doc : std::string_view{};
}

std::string_view StampedGrammar::spellingDoc(
    std::string_view text) const noexcept {
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

bool StampedGrammar::spellingFolds(std::string_view keyword) const noexcept {
  for (std::uint16_t t = 0; t < _f->terminalCount; ++t) {
    const auto& term = _f->terminals[t];
    if (!term.folded) continue;
    for (const std::string_view spelling : term.spellings)
      if (folded_equal(spelling, keyword)) return true;
  }
  return false;
}

std::string_view StampedGrammar::recordDoc(const CstNode& n) const noexcept {
  const ElementId e = _stamp.row(n);
  return holds(e) ? _f->names[answer(e).doc] : std::string_view{};
}

}  // namespace cursiv::engine
