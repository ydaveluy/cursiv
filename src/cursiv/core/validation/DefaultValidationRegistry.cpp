#include "cursiv/core/validation/DefaultValidationRegistry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/validation/DocumentValidator.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "cursiv/core/validation/ValidationCategory.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {
namespace {

[[nodiscard]] bool runs(ValidationCategories categories,
                        const ValidationCategory* category) noexcept {
  return categories.empty() ||
         std::ranges::find(categories, category) != categories.end();
}

// A check that throws does not abort the pass: the failure becomes a diagnostic
// on the node it was looking at and the walk carries on, so one broken check
// costs one node and stays visible to its author.
//
// A cancellation is not a failure and goes straight through: turning it into a
// diagnostic would publish a crash on every document of a cancelled build.
void run_one(const ValidationCheck& check, const AstNode& node,
             ValidationAcceptor& accept, const CancellationToken& token,
             const CoreServices& core) {
  const auto failed = [&](std::string_view what) {
    accept.accept(node, DiagnosticSeverity::Error,
                  std::format("An error occurred during validation: {}", what),
                  kValidationError);
    const Document* const home = node.getDocument();
    core.shared.observabilitySink->publish(
        {.severity = ObservationSeverity::Warning,
         .code = ObservationCode::ValidationCheckThrew,
         .message = std::format("a validation check threw: {}", what),
         .uri = home != nullptr ? home->uri : std::string(),
         .languageId = std::string(core.languageMetaData.languageId)});
  };
  try {
    check(node, accept, token);
  } catch (const OperationCancelled&) {
    throw;
  } catch (const std::exception& failure) {
    failed(failure.what());
  } catch (...) {
    failed("unknown exception");
  }
}

}  // namespace

// Multiplicative hashing: the product's upper half is the well-mixed one.
std::size_t DefaultValidationRegistry::homeOf(
    const AstTypeTag* type) const noexcept {
  return static_cast<std::size_t>(
             (std::uint64_t{reinterpret_cast<std::uintptr_t>(type)} * _mul) >>
             32u) &
         _typeMask;
}

std::size_t DefaultValidationRegistry::slotOf(
    const AstTypeTag* type) const noexcept {
  std::size_t at = homeOf(type);
  while (_byType[at].type != nullptr && _byType[at].type != type)
    at = (at + 1) & _typeMask;
  return at;
}

void DefaultValidationRegistry::rehash(std::size_t size) {
  std::vector<Row> old = std::exchange(_byType, std::vector<Row>(size));
  _typeMask = size - 1;
  for (Row& row : old)
    if (row.type != nullptr) _byType[slotOf(row.type)] = std::move(row);
}

std::vector<DefaultValidationRegistry::Entry>&
DefaultValidationRegistry::entriesOf(const AstTypeTag* type) {
  if (_typeCount != 0)
    if (Row& row = _byType[slotOf(type)]; row.type == type) return row.entries;
  if ((_typeCount + 1) * 4 > _byType.size())
    rehash(std::max<std::size_t>(16, _byType.size() * 2));
  _byType[slotOf(type)].type = type;
  ++_typeCount;
  const auto homed = [&] {
    for (std::size_t at = 0; at < _byType.size(); ++at)
      if (_byType[at].type != nullptr && homeOf(_byType[at].type) != at)
        return false;
    return true;
  };
  // A collision tries other multipliers before a wider table: which tags
  // collide is the addresses', not the grammar's.
  for (unsigned tries = 1; !homed() && tries <= 64; ++tries) {
    _mul += 0x2545f4914f6cdd1dull << 1u;
    rehash(tries % 8 == 0 && _byType.size() < 16 * _typeCount
               ? _byType.size() * 2
               : _byType.size());
  }
  _homeDecides = homed() && _everyNode.empty();
  return _byType[slotOf(type)].entries;
}

void DefaultValidationRegistry::fileDocumentCheck(
    std::vector<ValidationPreparationEntry>& into, ValidationPreparation check,
    const ValidationCategory* category) {
  if (!check) return;
  // The reserved category, refused as discovery refuses it for a per-node
  // check: a hook hiding in `built_in` would change what that half means.
  if (category == nullptr || category == &built_in)
    throw ValidationRegistrationError(
        "a document check needs a category of its own -- `built_in` is the "
        "runtime's and null is nobody's");
  if (std::ranges::find(_categories, category) == _categories.end())
    _categories.push_back(category);
  into.push_back(ValidationPreparationEntry{category, std::move(check)});
}

// -Warray-bounds fires here on a `std::mutex` this function never names: at -O3
// g++ inlines `shared_ptr::_M_release` and confuses two
// `_Sp_counted_ptr_inplace` instantiations.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
void DefaultValidationRegistry::registerCheck(
    ValidationCheckRegistration registration,
    const ValidationCategory* category) {
  if (!registration.check) return;
  // The reserved category, refused as it is for a document hook: a check hiding
  // in `built_in` would change what that half means.
  if (category == nullptr || category == &built_in)
    throw ValidationRegistrationError(
        "a check needs a category of its own -- `built_in` is the runtime's "
        "and null is nobody's");
  // The catch-all needs no reflection: `cursiv::AstNode` is every node.
  // Otherwise, an exact check is filed under its own tag alone; the ordinary
  // one under every tag of the family, which is what "a check written over a
  // base" means.
  const bool catchAll = registration.target == nullptr;
  std::span<const AstTypeTag* const> family;
  if (!catchAll) {
    family = _core.astReflection->getAllSubTypes(registration.target);
    // The refusal comes first: a registration that throws must leave no trace,
    // neither a count nor a published category.
    //
    // A target no reflection names is refused loudly: its family is empty, so a
    // check filed under the bare tag could never fire -- no produced node
    // carries a base's tag -- and it would be counted and its category
    // published while it never ran. Discovery refuses this at compile time for
    // a language's own checks; here, at the erased door, the usual cause is
    // bootstrap order, the grammar's reflection added after the checks.
    if (family.empty())
      throw ValidationRegistrationError(
          std::format("the check's target type '{}' is not in the workspace "
                      "reflection -- no node could ever match, so the check "
                      "would silently never run. Add the grammar's reflection "
                      "before its checks (`install_language` does), or fix "
                      "the target",
                      registration.target->name));
    if (registration.exactTypeOnly)
      family = std::span<const AstTypeTag* const>(&registration.target, 1);
  }
  ++_count;
  if (std::ranges::find(_categories, category) == _categories.end())
    _categories.push_back(category);
  const Entry entry{category, std::make_shared<const ValidationCheck>(
                                  std::move(registration.check))};
  if (catchAll) {
    _everyNode.push_back(entry);
    _homeDecides = false;
    return;
  }
  for (const AstTypeTag* tag : family) entriesOf(tag).push_back(entry);
}
#pragma GCC diagnostic pop

void DefaultValidationRegistry::runChecks(
    const AstNode& node, ValidationAcceptor& accept,
    ValidationCategories categories, const CancellationToken& token) const {
  if (_homeDecides &&
      _byType[homeOf(node.type)].type != node.type)
    return;
  runMatched(node, accept, categories, token);
}

// Out of line, so a node turned away above pays no frame.
CURSIV_NOINLINE void DefaultValidationRegistry::runMatched(
    const AstNode& node, ValidationAcceptor& accept,
    ValidationCategories categories, const CancellationToken& token) const {
  if (_typeCount != 0)
    if (const Row& typed = _byType[slotOf(node.type)]; typed.type != nullptr)
      for (const Entry& entry : typed.entries)
        if (runs(categories, entry.category))
          run_one(*entry.check, node, accept, token, _core);
  // ...then the ones no type filters, after the typed ones: a published
  // diagnostic list is read by humans, so its order is part of the answer.
  for (const Entry& entry : _everyNode)
    if (runs(categories, entry.category))
      run_one(*entry.check, node, accept, token, _core);
}

}  // namespace cursiv
