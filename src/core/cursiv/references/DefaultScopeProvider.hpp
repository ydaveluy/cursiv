#pragma once

#include <string_view>

#include "cursiv/references/ScopeProvider.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The scope is the containers of the reference site, innermost first. A name
// declared closer shadows nothing: both are visited, in that order.
//
// Not templated: the one grammar question it asks -- may this candidate stand
// here -- goes through the `AstReflection` service.

namespace cursiv {

// Not `final`: the most-overridden service there is. A language that makes a
// standard library or an import visible changes one thing -- what the workspace
// declares once no container up the tree does -- which is the three reads of
// the global scope below.
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
  // The case policy. With no scope objects to carry a flag, the walk asks
  // this instead -- once per query, for the whole climb and the global scope
  // behind it. A case-insensitive language overrides it; matching never
  // changes what a description spells, only what it answers to.
  [[nodiscard]] virtual bool isCaseSensitive(
      const ReferenceInfo&) const noexcept {
    return true;
  }

  // The global scope, and it is what makes a workspace one: a name no container
  // up the tree declares is looked for in what every other document exported.
  // Three hooks because a scope here is not an object but three ways of reading
  // it -- the first entry of one name for a single reference, every entry of it
  // for a multi reference, all of them for completion. A policy changes the
  // three together, or linking and completion disagree. The named reads above
  // record the name before asking these, so a later export of it relinks the
  // site; a provider that reads the index by another path extends
  // `IndexManager::isAffected`.
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
