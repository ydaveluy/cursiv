#include "cursiv/syntax-tree/ReferenceInfo.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"

namespace cursiv::detail {

std::optional<ReferenceInfo> reference_at_caret(const AstReflection& reflection,
                                                const Document& document,
                                                AstNode* container,
                                                const NextFeature& next,
                                                AstNodePtr& entered) {
  const std::string_view feature = next.property;
  if (feature.empty() || container == nullptr) return std::nullopt;

  std::uint16_t reference = kNoReference;
  if (next.type != nullptr) {
    // A newly entered rule owns the member even when an ancestor spells it too.
    reference = reflection.referenceIndexOf(next.type, feature);
    if (reference == kNoReference) return std::nullopt;
  } else {
    bool declared = false;
    for (AstNode* at = container; at != nullptr; at = at->parent) {
      reference = reflection.referenceIndexOf(at->type, feature);
      if (reference != kNoReference) break;
      const TypeMetaData meta = reflection.getTypeMetaData(at->type);
      for (const PropertyMetaData& q : meta.properties)
        declared = declared || q.name == feature;
      for (const AstTypeTag* base : meta.superTypes)
        for (const PropertyMetaData& q :
             reflection.getTypeMetaData(base).properties)
          declared = declared || q.name == feature;
    }
    if (reference == kNoReference) {
      if (declared) return std::nullopt;
      // A repair can remove the owner; only unanimous remaining types suffice.
      for (const AstTypeTag* type : reflection.getAllTypes()) {
        const std::uint16_t at = reflection.referenceIndexOf(type, feature);
        if (at == kNoReference) continue;
        if (reference != kNoReference && at != reference) return std::nullopt;
        reference = at;
      }
      if (reference == kNoReference) return std::nullopt;
    }
  }

  if (next.type != nullptr &&
      (next.entered || !reflection.isSubtype(container->type, next.type))) {
    entered = reflection.createNode(next.type);
    if (entered != nullptr) {
      entered->parent = container;
      container = entered.get();
    }
  }
  ReferenceInfo info;
  info.container = container;
  info.document = &document;
  info.reference = reference;
  info.referenceText = {};
  info.property = feature;
  // The site's own narrowing, which the tree can't say: the reference isn't
  // written yet, so only the grammar knows this site accepts less than the
  // member. Two sites of one member differ here, and their offers add up.
  info.narrow = next.narrow;
  return info;
}

}  // namespace cursiv::detail
