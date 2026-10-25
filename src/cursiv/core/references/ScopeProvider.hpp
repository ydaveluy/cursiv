#pragma once

#include <string_view>

#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

// What one reference site can see, asked three ways: everything visible, the
// first entry of one name, every entry of one name -- no scope object is
// built, the walk answers directly. What counts as "named `name`" is the
// provider's, never the caller's, so a language's case policy lives here.

namespace cursiv {

class ScopeProvider {
 public:
  virtual ~ScopeProvider() noexcept = default;

  // The first entry named `name` in declaration order, skipping the homonyms
  // that fail the type filter. Null when none, and null for an empty name.
  [[nodiscard]] virtual const AstNodeDescription* getScopeEntry(
      const ReferenceInfo& context, std::string_view name) const = 0;

  // Everything visible at the site, in lexical order and without
  // deduplicating -- which is how completion asks. A visitor returning false
  // stops the walk, and the return says whether it ran to the end.
  [[nodiscard]] virtual bool visitScopeEntries(
      const ReferenceInfo& context,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;

  // Every entry named `name` at the site -- inner levels first, the global
  // scope last -- skipping the homonyms that fail the type filter. Nothing
  // for an empty name.
  [[nodiscard]] virtual bool visitScopeEntries(
      const ReferenceInfo& context, std::string_view name,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const = 0;
};

}  // namespace cursiv
