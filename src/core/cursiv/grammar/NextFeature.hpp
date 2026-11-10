#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"

// One thing that may be written at a point, and the context the grammar gives
// it. Readings of one token that fill different members are one entry each.

namespace cursiv {

struct NextFeature {
  // The element itself -- a literal to insert, a token category to name.
  ElementId feature = kNoElement;
  // The member being filled, empty when none. This is what turns a token into
  // a cross-reference: the grammar names the member, the tree names the class.
  std::string_view property{};
  // What the site accepts, which may be less than the member does: an index
  // into the member's `RefOps::narrows`, `kNoNarrow` where it narrows nothing.
  std::uint8_t narrow = kNoNarrow;
  // The class that carries `property`. The tree cannot always say it: the
  // record the member belongs to may not be open yet at the caret, and the
  // same member name on two classes is two different references.
  const AstTypeTag* type = nullptr;
  // The caret is at the start of the rule that writes the member: none of its
  // tokens is before the caret, so its node is not in the tree yet.
  bool entered = false;

  bool operator==(const NextFeature&) const = default;
};

}  // namespace cursiv
