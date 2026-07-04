#include <pegium/core/references/DefaultScopeProvider.hpp>

#include <algorithm>
#include <memory>
#include <typeindex>
#include <typeinfo>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/syntax-tree/AstReflection.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/workspace/AstDescriptions.hpp>

namespace pegium::references {
namespace {

using AstNodeDescription = workspace::AstNodeDescription;
using ScopeEntryBucket = workspace::ScopeEntryBucket;
using DescriptionVisitor =
    utils::function_ref<bool(const AstNodeDescription &)>;

[[nodiscard]] bool visit_entry_pointers(
    const std::vector<const AstNodeDescription *> &entries,
    DescriptionVisitor visitor) {
  return std::ranges::all_of(entries, [&visitor](const auto *entry) {
    return visitor(*entry);
  });
}

template <typename Entries>
[[nodiscard]] bool visit_owned_entries(const Entries &entries,
                                       DescriptionVisitor visitor) {
  return std::ranges::all_of(
      entries, [&visitor](const auto &entry) { return visitor(entry); });
}

[[nodiscard]] bool visit_named_entries(
    const workspace::NamedScopeEntries &entries, DescriptionVisitor visitor) {
  if (entries.empty()) {
    return true;
  }
  if (!visitor(*entries.first)) {
    return false;
  }
  return visit_entry_pointers(entries.duplicates, visitor);
}

[[nodiscard]] bool accepts_bucket(
    const pegium::SharedCoreServices &shared, std::type_index referenceType,
    const ScopeEntryBucket &bucket) {
  return type_is_assignable(bucket.type, referenceType, *shared.astReflection);
}

[[nodiscard]] const AstNodeDescription *
find_scope_entry(const pegium::SharedCoreServices &shared,
                 const workspace::BucketedScopeEntries &entries,
                 std::type_index referenceType,
                 const utils::HashedStringView &name) noexcept {
  for (const auto &bucket : entries) {
    // Probe the (O(1), hash-cached) name index first; only pay the bucket type
    // check (isSubtype) for buckets that actually hold the name. Same
    // conjunction and iteration order as before, so the selected entry is
    // identical.
    const auto it = bucket.entriesByName.find(name);
    if (it == bucket.entriesByName.end()) {
      continue;
    }
    if (!accepts_bucket(shared, referenceType, bucket)) {
      continue;
    }
    return it->second.firstEntry();
  }
  return nullptr;
}

[[nodiscard]] bool visit_entries(const pegium::SharedCoreServices &shared,
                                 const workspace::BucketedScopeEntries &entries,
                                 std::type_index referenceType,
                                 const utils::HashedStringView &name,
                                 DescriptionVisitor visitor) {
  for (const auto &bucket : entries) {
    if (name.value.empty()) {
      // Whole-bucket visit: the type check is the only filter.
      if (!accepts_bucket(shared, referenceType, bucket)) {
        continue;
      }
      if (!visit_owned_entries(bucket.ownedEntries, visitor)) {
        return false;
      }
      continue;
    }

    // Named visit: probe the name index first; only type-check buckets that
    // hold the name (same conjunction/order as before).
    const auto it = bucket.entriesByName.find(name);
    if (it == bucket.entriesByName.end()) {
      continue;
    }
    if (!accepts_bucket(shared, referenceType, bucket)) {
      continue;
    }
    if (!visit_named_entries(it->second, visitor)) {
      return false;
    }
  }
  return true;
}

template <typename Visitor>
bool visit_local_scope_levels(const workspace::LocalSymbols &localSymbols,
                              const AstNode *container, Visitor &&visitor) {
  for (auto *current = container; current != nullptr;
       current = current->getContainer()) {
    const auto *entries = localSymbols.forContainer(current);
    if (entries == nullptr) {
      continue;
    }
    if (!visitor(*entries)) {
      return false;
    }
  }
  return true;
}

} // namespace

DefaultScopeProvider::DefaultScopeProvider(
    const pegium::CoreServices &services)
    : pegium::DefaultCoreService(services), _globalScopeCache(services.shared) {}

const workspace::AstNodeDescription *DefaultScopeProvider::getScopeEntry(
    const ReferenceInfo &context) const {
  if (context.referenceText.empty()) {
    return nullptr;
  }

  const auto referenceType = context.getReferenceType();
  // Hash the reference text once; the key is reused across every bucket and
  // ancestor scope level (and the global index) without rehashing.
  const utils::HashedStringView nameKey{context.referenceText};
  if (const auto *container = context.container; container != nullptr) {
    const auto &localSymbols = getDocument(*container).localSymbols;
    const AstNodeDescription *entry = nullptr;
    (void)visit_local_scope_levels(
        localSymbols, container,
        [this, referenceType, &nameKey,
         &entry](const workspace::BucketedScopeEntries &entries) {
          entry = find_scope_entry(services.shared, entries, referenceType,
                                   nameKey);
          return entry == nullptr;
        });
    if (entry != nullptr) {
      return entry;
    }
  }

  const auto globalEntries = getGlobalEntries(referenceType, context);
  const auto globalIt = globalEntries->entriesByName.find(nameKey);
  if (globalIt == globalEntries->entriesByName.end()) {
    return nullptr;
  }
  return globalIt->second.firstEntry();
}

bool DefaultScopeProvider::visitScopeEntries(
    const ReferenceInfo &context,
    utils::function_ref<bool(const workspace::AstNodeDescription &)> visitor)
    const {
  const auto referenceType = context.getReferenceType();
  const utils::HashedStringView nameKey{context.referenceText};
  if (const auto *container = context.container; container != nullptr) {
    const auto &localSymbols = getDocument(*container).localSymbols;
    if (!visit_local_scope_levels(
            localSymbols, container,
            [this, referenceType, &nameKey,
             visitor](const workspace::BucketedScopeEntries &entries) {
              return visit_entries(services.shared, entries, referenceType,
                                   nameKey, visitor);
            })) {
      return false;
    }
  }

  const auto globalEntries = getGlobalEntries(referenceType, context);
  if (context.referenceText.empty()) {
    return visit_owned_entries(globalEntries->elements, visitor);
  }

  const auto globalIt = globalEntries->entriesByName.find(nameKey);
  if (globalIt == globalEntries->entriesByName.end()) {
    return true;
  }
  return visit_named_entries(globalIt->second, visitor);
}

std::shared_ptr<const DefaultScopeProvider::CompiledGlobalEntries>
DefaultScopeProvider::getGlobalEntries(std::type_index referenceType,
                                       const ReferenceInfo & /*context*/) const {
  return _globalScopeCache.get(
      referenceType,
      [this,
       referenceType]() -> std::shared_ptr<const CompiledGlobalEntries> {
        auto compiled = std::make_shared<CompiledGlobalEntries>();
        if (referenceType == std::type_index(typeid(void))) {
          return compiled;
        }

        compiled->elements =
            services.shared.workspace.indexManager->allElements(referenceType);
        compiled->entriesByName.reserve(compiled->elements.size());
        for (const auto &entry : compiled->elements) {
          compiled->entriesByName[entry.name].add(entry);
        }
        return compiled;
      });
}

} // namespace pegium::references
