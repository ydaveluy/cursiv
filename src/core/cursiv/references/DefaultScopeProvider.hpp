#pragma once

#include <string_view>

#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The scope is the reference site's containers, innermost first; an inner name
// shadows nothing, both are visited in order. Type checks go through
// `AstReflection`.

namespace cursiv {

class DefaultScopeProvider : public ScopeProvider,
                             protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] const AstNodeDescription* getScopeEntry(
      const ReferenceInfo& context, std::string_view name) const override;

  [[nodiscard]] bool visitScopeEntries(
      const ReferenceInfo& context,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor)
      const override;
  [[nodiscard]] bool visitScopeEntries(
      const ReferenceInfo& context, std::string_view name,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor)
      const override;
  ~DefaultScopeProvider() noexcept override = default;

 protected:
  // The case policy, asked once per query. Matching never changes what a
  // description spells, only what it answers to.
  [[nodiscard]] virtual bool isCaseSensitive(
      const ReferenceInfo&) const noexcept {
    return true;
  }

  // The global scope: what other documents exported. Three reads (one entry,
  // every entry of a name, all entries) that a policy changes together. A
  // provider reading the index another way extends `IndexManager::isAffected`.
  [[nodiscard]] virtual const AstNodeDescription* getGlobalScopeEntry(
      const ReferenceInfo& context, std::string_view name, AcceptSet accepts,
      const AstReflection& reflection, bool caseSensitive) const;
  [[nodiscard]] virtual bool visitGlobalScopeEntries(
      const ReferenceInfo& context, AcceptSet accepts,
      const AstReflection& reflection,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const;
  [[nodiscard]] virtual bool visitGlobalScopeEntries(
      const ReferenceInfo& context, std::string_view name, AcceptSet accepts,
      const AstReflection& reflection, bool caseSensitive,
      cursiv::function_ref<bool(const AstNodeDescription&)> visitor) const;
};

}  // namespace cursiv
