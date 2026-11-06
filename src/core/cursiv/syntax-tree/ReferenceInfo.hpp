#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"

// One reference site, as plain data: everything here can be filled by hand,
// since a content assist has no reference object, only a caret in a
// container asking what may be written there. `reference` names that
// member's row in the grammar -- what a caret can state and an object is not.

namespace cursiv {

// No reference member: a site that filters nothing.
inline constexpr std::uint16_t kNoReference = 0xFFFFu;

struct AstReflection;
struct Document;
struct NextFeature;

struct ReferenceInfo {
  const AstNode* container = nullptr;
  // The document the site is in, carried rather than climbed to. Null only
  // for a site read outside a linked document.
  const Document* document = nullptr;
  // What the site spells -- what the linker resolves. Distinct from the name
  // a scope query looks up, which a completion varies over one site.
  std::string_view referenceText;
  std::string_view property;
  std::optional<std::uint32_t> index;

  // What may stand here, as the grammar indexes it: the row of the reference
  // member, which is what its accept table is keyed by. `kNoReference`
  // accepts everything, which is what an unfiltered enumeration wants.
  //
  // An index and not a tag, for the same reason a description carries one: a
  // tag is process-wide and would have to be looked up, an index is the
  // language's own and indexes directly. A caller holding only a type asks
  // `AstReflection::referenceIndexOf`.
  std::uint16_t reference = kNoReference;

  // The site's narrowing, stamped by the writer: what this site accepts may
  // be less than the member does, and only the site knows. An index into
  // the member's `RefOps::narrows`; `kNoNarrow` for a site that does not
  // narrow.
  std::uint8_t narrow = kNoNarrow;
};

namespace detail {

// The cross-reference `next` fills with `container` at the caret, if its
// member holds one: what a completion and the MCP candidates both ask the
// scope with, so the two answer alike. Where the caret opens the rule that
// writes the member, or the member belongs to a class `container` is not, a
// node of that class is made for the occasion, as a child of `container`,
// and `entered` owns it: the answer's `container` holds as long as `entered`
// does. No caret node answers no scope.
[[nodiscard]] std::optional<ReferenceInfo> reference_at_caret(
    const AstReflection& reflection, const Document& document,
    AstNode* container, const NextFeature& next, AstNodePtr& entered);

}  // namespace detail

}  // namespace cursiv
