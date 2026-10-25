#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"

// One reference site, as plain data: everything here can be filled by hand,
// since a content assist has no reference object, only a caret in a
// container asking what may be written there. `reference` names that
// member's row in the grammar -- what a caret can state and an object is not.

namespace cursiv {

// No reference member: a site that filters nothing.
inline constexpr std::uint16_t kNoReference = 0xFFFFu;

struct Document;

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

}  // namespace cursiv
