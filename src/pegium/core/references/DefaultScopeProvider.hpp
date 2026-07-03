#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <pegium/core/references/ScopeProvider.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/syntax-tree/Reference.hpp>
#include <pegium/core/utils/Caching.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <pegium/core/workspace/IndexManager.hpp>

namespace pegium {
struct CoreServices;
}

namespace pegium::references {

/// Default scope provider combining local symbols with globally indexed exports.
class DefaultScopeProvider : public ScopeProvider,
                             protected pegium::DefaultCoreService {
public:
  /// Creates a scope provider backed by the shared caches and index manager from `services`.
  explicit DefaultScopeProvider(const pegium::CoreServices &services);

  /// Returns the first visible match for `context.referenceText`.
  [[nodiscard]] const workspace::AstNodeDescription *
  getScopeEntry(const ReferenceInfo &context) const override;

  /// Visits all visible local then global entries matching `context`.
  [[nodiscard]] bool visitScopeEntries(
      const ReferenceInfo &context,
      utils::function_ref<bool(const workspace::AstNodeDescription &)> visitor)
      const override;

protected:
  /// Cached view of all globally exported entries accepted by one reference type.
  struct CompiledGlobalEntries {
    using NameIndex = workspace::NamedScopeEntryIndex;

    std::vector<workspace::AstNodeDescription> elements;
    NameIndex entriesByName;
  };

  /// Returns the globally indexed entries accepted by `referenceType`.
  [[nodiscard]] virtual std::shared_ptr<const CompiledGlobalEntries>
  getGlobalEntries(std::type_index referenceType) const;

  mutable utils::WorkspaceCache<std::type_index,
                                std::shared_ptr<const CompiledGlobalEntries>>
      _globalScopeCache;
};

} // namespace pegium::references
