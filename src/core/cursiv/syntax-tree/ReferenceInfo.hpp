#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"

// One reference site, as plain data a content assist can fill by hand: a
// caret in a container, asking what may be written there.

namespace cursiv {

// No reference member: a site that filters nothing.
inline constexpr std::uint16_t kNoReference = 0xFFFFu;

struct AstReflection;
struct Document;
struct NextFeature;

struct ReferenceInfo {
  const AstNode* container = nullptr;
  // The document the site is in, carried rather than walked up to. Null only
  // for a site read outside a linked document.
  const Document* document = nullptr;
  // What the site spells -- what the linker resolves. Distinct from the name
  // a scope query looks up, which a completion varies over one site.
  std::string_view referenceText;
  std::string_view property;
  std::optional<std::uint32_t> index;

  // The row of the reference member, which keys its accept table.
  // `kNoReference` accepts everything. From a type, ask
  // `AstReflection::referenceIndexOf`.
  std::uint16_t reference = kNoReference;

  // The site's narrowing: an index into the member's `RefOps::narrows`, or
  // `kNoNarrow`.
  std::uint8_t narrow = kNoNarrow;

  // What the lookup of a failed site threw, for its diagnostic. Empty
  // otherwise.
  std::string_view failure = {};
};

namespace detail {

// The site's document, carried by the reference table rather than walked up
// to, or found from the container for a site outside a linked document.
[[nodiscard]] inline const Document* document_of(
    const ReferenceInfo& reference) noexcept {
  if (reference.document != nullptr) return reference.document;
  return reference.container == nullptr ? nullptr
                                        : reference.container->getDocument();
}

// The cross-reference `next` fills with `container` at the caret, as a
// completion asks the scope. A node made for the occasion is owned by
// `entered`, and the answer holds as long as it does.
[[nodiscard]] std::optional<ReferenceInfo> reference_at_caret(
    const AstReflection& reflection, const Document& document,
    AstNode* container, const NextFeature& next, AstNodePtr& entered);

}  // namespace detail

}  // namespace cursiv
