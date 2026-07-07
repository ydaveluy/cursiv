#pragma once

/// Grammar contract for character-class terminals.

#include <array>

#include <pegium/core/grammar/AbstractElement.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>

namespace pegium::grammar {

struct CharacterRange : AbstractElement {
  constexpr CharacterRange() noexcept : AbstractElement(ElementKind::CharacterRange) {}
  constexpr ~CharacterRange() noexcept override = default;
  virtual std::string_view getValue(const CstNodeView &node) const noexcept;
  /// Per-byte membership table of this class's alphabet. Read by the
  /// grammar-derived word-character-mask walk (`derive_word_char_mask`).
  virtual const std::array<bool, 256> &characterMask() const noexcept = 0;
};

} // namespace pegium::grammar
