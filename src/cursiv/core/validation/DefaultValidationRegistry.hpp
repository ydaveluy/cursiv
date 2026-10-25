#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"

// The index is built at registration, not walked at dispatch: a check on a
// base is filed under every concrete type of its family (read from the
// shared reflection, the same table the scope provider and the index filter
// references with), so running a node's checks is one lookup on its tag. A
// check is shared, not copied: copying the `std::function` would copy
// whatever it captured.

namespace cursiv {

struct CoreServices;

class DefaultValidationRegistry : public ValidationRegistry {
 public:
  explicit DefaultValidationRegistry(const CoreServices& core) noexcept
      : _core(core) {}

  // Precondition: the grammar's reflection is already on the workspace union.
  void registerCheck(ValidationCheckRegistration,
                     const ValidationCategory* category) override;

  void registerBeforeDocument(ValidationPreparation check,
                              const ValidationCategory* category) override {
    fileDocumentCheck(_before, std::move(check), category);
  }
  void registerAfterDocument(ValidationPreparation check,
                             const ValidationCategory* category) override {
    fileDocumentCheck(_after, std::move(check), category);
  }
  [[nodiscard]] std::span<const ValidationPreparationEntry> checksBefore()
      const noexcept override {
    return _before;
  }
  [[nodiscard]] std::span<const ValidationPreparationEntry> checksAfter()
      const noexcept override {
    return _after;
  }

  void runChecks(const AstNode&, ValidationAcceptor&, ValidationCategories,
                 const CancellationToken&) const override;

  [[nodiscard]] std::size_t checkCount() const noexcept override {
    return _count;
  }
  [[nodiscard]] std::span<const ValidationCategory* const> categories()
      const noexcept override {
    return _categories;
  }

 private:
  struct Entry {
    const ValidationCategory* category;
    std::shared_ptr<const ValidationCheck> check;
  };
  struct Row {
    const AstTypeTag* type = nullptr;
    std::vector<Entry> entries;
  };

  // Refuses `built_in` (reserved) and a null check; notes the category.
  void fileDocumentCheck(std::vector<ValidationPreparationEntry>& into,
                         ValidationPreparation check,
                         const ValidationCategory* category);
  [[nodiscard]] std::size_t homeOf(const AstTypeTag* type) const noexcept;
  // The slot holding `type`, or the empty one where it would go.
  [[nodiscard]] std::size_t slotOf(const AstTypeTag* type) const noexcept;
  [[nodiscard]] std::vector<Entry>& entriesOf(const AstTypeTag* type);
  void rehash(std::size_t size);
  void runMatched(const AstNode&, ValidationAcceptor&, ValidationCategories,
                  const CancellationToken&) const;

  const CoreServices& _core;
  // Open-addressed on the tag, power of two, at most a quarter full.
  std::vector<Row> _byType;
  std::size_t _typeMask = 0;
  std::size_t _typeCount = 0;
  // Odd, so the product stays a bijection: a new multiplier can part tags.
  std::uint64_t _mul = 0x9e3779b97f4a7c15ull;
  // Every filed tag on its home slot, and no check on every node: a node's
  // home slot then decides alone, and the many nodes no check watches are
  // turned away by one compare.
  bool _homeDecides = false;
  // The checks no type filters -- those whose target is `cursiv::AstNode`.
  std::vector<Entry> _everyNode;
  // The document-level hooks, in registration order.
  std::vector<ValidationPreparationEntry> _before;
  std::vector<ValidationPreparationEntry> _after;
  // Registrations, not index rows: one check on a base counts once.
  std::size_t _count = 0;
  std::vector<const ValidationCategory*> _categories;
};

}  // namespace cursiv
