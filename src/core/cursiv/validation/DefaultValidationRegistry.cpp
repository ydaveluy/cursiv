#include "cursiv/validation/DefaultValidationRegistry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {
namespace {

// Room for one more element, grown as `push_back` grows, so the push that
// follows cannot fail half-way through a registration.
template <class T>
void room_for_one(std::vector<T>& elements) {
  if (elements.size() == elements.capacity())
    elements.reserve(std::max<std::size_t>(4, elements.capacity() * 2));
}

// A check that throws becomes a diagnostic on its node, and the walk goes on.
// A cancellation goes straight through.
void run_one(const ValidationRegistry& registry, const ValidationCheck& check,
             const AstNode& node, ValidationAcceptor& accept,
             const CancellationToken& token) {
  try {
    check(node, accept, token);
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    registry.handleException(std::current_exception(),
                             "An error occurred during validation", accept,
                             &node);
  }
}

}  // namespace

// What threw is read off `failure`; an exception that is no `std::exception`
// says so.
void DefaultValidationRegistry::handleException(std::exception_ptr failure,
                                                std::string_view context,
                                                ValidationAcceptor& accept,
                                                const AstNode* node) const {
  std::string what = "unknown exception";
  try {
    if (failure != nullptr) std::rethrow_exception(failure);
  } catch (const std::exception& thrown) {
    what = thrown.what();
  } catch (...) {  // NOLINT(bugprone-empty-catch): `what` already says it
  }
  const Document& document = accept.document();
  const TextRange span =
      node != nullptr
          ? accept.spanOf(*node)
          : TextRange{0, static_cast<TextOffset>(
                             document.textDocument().getText().size())};
  Diagnostic diagnostic{span,
                        DiagnosticSeverity::Error,
                        std::format("{}: {}", context, what),
                        {},
                        {}};
  set_data_code(diagnostic, kValidationError);
  accept.accept(std::move(diagnostic));
  services.shared().observabilitySink->publish(
      {.severity = ObservationSeverity::Warning,
       .code = node != nullptr ? ObservationCode::ValidationCheckThrew
                               : ObservationCode::ValidationPreparationThrew,
       .message =
           std::string(node != nullptr ? "a validation check threw: "
                                       : "a validation preparation threw: ") +
           what,
       .uri = document.uri,
       .languageId = std::string(services.languageMetaData.languageId)});
}

// Multiplicative hashing: the product's upper half is the well-mixed one.
std::size_t DefaultValidationRegistry::hashSlotOf(
    const AstTypeTag* type) const noexcept {
  return static_cast<std::size_t>(
             (std::uint64_t{reinterpret_cast<std::uintptr_t>(type)} * _mul) >>
             32u) &
         _typeMask;
}

std::size_t DefaultValidationRegistry::slotOf(
    const AstTypeTag* type) const noexcept {
  std::size_t at = hashSlotOf(type);
  while (_byType[at].type != nullptr && _byType[at].type != type)
    at = (at + 1) & _typeMask;
  return at;
}

// The multiplier changes with the table it places, once the table is
// allocated: a refused allocation leaves both as they were.
void DefaultValidationRegistry::rehash(std::size_t size, std::uint64_t mul) {
  std::vector<Row> old = std::exchange(_byType, std::vector<Row>(size));
  _typeMask = size - 1;
  _mul = mul;
  for (Row& row : old)
    if (row.type != nullptr) _byType[slotOf(row.type)] = std::move(row);
}

std::vector<DefaultValidationRegistry::Entry>&
DefaultValidationRegistry::entriesOf(const AstTypeTag* type) {
  if (_typeCount != 0)
    if (Row& row = _byType[slotOf(type)]; row.type == type) return row.entries;
  if ((_typeCount + 1) * 4 > _byType.size())
    rehash(std::max<std::size_t>(16, _byType.size() * 2), _mul);
  _byType[slotOf(type)].type = type;
  ++_typeCount;
  // Off until the search below has run: a refused one leaves this tag where
  // only the full probe finds it.
  _hashSlotDecides = false;
  const auto allAtHashSlot = [&] {
    for (std::size_t at = 0; at < _byType.size(); ++at)
      if (_byType[at].type != nullptr && hashSlotOf(_byType[at].type) != at)
        return false;
    return true;
  };
  // A collision tries other multipliers before a wider table: which tags
  // collide is the addresses', not the grammar's.
  for (unsigned tries = 1; !allAtHashSlot() && tries <= 64; ++tries)
    rehash(tries % 8 == 0 && _byType.size() < 16 * _typeCount
               ? _byType.size() * 2
               : _byType.size(),
           _mul + (0x2545f4914f6cdd1dull << 1u));
  _hashSlotDecides = allAtHashSlot() && _everyNode.empty();
  return _byType[slotOf(type)].entries;
}

void DefaultValidationRegistry::fileDocumentCheck(
    std::vector<DocumentCheckEntry>& into, DocumentCheck check,
    const ValidationCategory* category) {
  if (!check) return;
  // The reserved category, refused as discovery refuses it for a per-node
  // check: a hook hiding in `built_in` would change what that half means.
  if (category == nullptr || category == &built_in)
    throw ValidationRegistrationError(
        "a document check needs a category of its own -- `built_in` is the "
        "runtime's and null is nobody's");
  // The hook first, so a refused one publishes no category.
  const bool newCategory =
      std::ranges::find(_categories, category) == _categories.end();
  if (newCategory) room_for_one(_categories);
  into.emplace_back(category, std::move(check));
  if (newCategory) _categories.push_back(category);
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
  // A catch-all check needs no reflection. Otherwise an exact check is filed
  // under its own tag, and an ordinary one under every tag of its family.
  const bool catchAll = registration.target == nullptr;
  std::span<const AstTypeTag* const> family;
  if (!catchAll) {
    family = services.astReflection->getAllSubTypes(registration.target);
    // Refused before anything changes: a target no reflection names could never
    // fire. The usual cause is a grammar's reflection added after its checks.
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
  // Every allocation before the first change: a refusal files the check under
  // no tag of its family, and a retry files it once everywhere.
  const Entry entry{category, std::make_shared<const ValidationCheck>(
                                  std::move(registration.check))};
  const bool newCategory =
      std::ranges::find(_categories, category) == _categories.end();
  if (newCategory) room_for_one(_categories);
  if (catchAll)
    room_for_one(_everyNode);
  else
    for (const AstTypeTag* tag : family) room_for_one(entriesOf(tag));
  ++_count;
  if (newCategory) _categories.push_back(category);
  if (catchAll) {
    _everyNode.push_back(entry);
    _hashSlotDecides = false;
    return;
  }
  for (const AstTypeTag* tag : family) entriesOf(tag).push_back(entry);
}
#pragma GCC diagnostic pop

void DefaultValidationRegistry::runChecks(
    const AstNode& node, ValidationAcceptor& accept,
    ValidationCategories categories, const CancellationToken& token) const {
  if (_hashSlotDecides && _byType[hashSlotOf(node.type)].type != node.type)
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
        if (detail::runs_category(categories, entry.category))
          run_one(*this, *entry.check, node, accept, token);
  // Then the checks no type filters, after the typed ones: people read the
  // diagnostic list, so its order matters.
  for (const Entry& entry : _everyNode)
    if (detail::runs_category(categories, entry.category))
      run_one(*this, *entry.check, node, accept, token);
}

}  // namespace cursiv
