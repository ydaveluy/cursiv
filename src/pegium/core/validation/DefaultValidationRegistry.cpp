#include <pegium/core/validation/DefaultValidationRegistry.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pegium/core/observability/ObservabilitySink.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/utils/Errors.hpp>
#include <pegium/core/utils/TypeIndexHash.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>

namespace pegium::validation {

namespace detail {

struct CompiledValidationCheckEntry {
  std::type_index targetType = std::type_index(typeid(AstNode));
  ValidationCheck check; // unwrapped: try/catch is hoisted to runChecks
  ValidationCheckMatches matches = nullptr;
  bool exactTypeOnly = false;
  std::string category;
};

using CompiledValidationCheckList =
    std::vector<const CompiledValidationCheckEntry *>;
using CompiledValidationCheckIndex =
    std::unordered_map<std::type_index, CompiledValidationCheckList,
                       utils::FastTypeIndexHash, utils::FastTypeIndexEqual>;

struct CompiledValidationRegistry {
  std::vector<std::string> knownCategories;
  std::vector<CompiledValidationCheckEntry> checks;
  CompiledValidationCheckIndex checksByType;
  // Checks whose target is an abstract base the grammar never named (absent
  // from reflection): they cannot be pre-indexed by concrete subtype, so
  // `runChecks` gates them per node with the entry's `matches` predicate.
  CompiledValidationCheckList fallbackChecks;
  std::shared_ptr<observability::ObservabilitySink> sink;
};

} // namespace detail

namespace {

void publish_validation_observation(
    observability::ObservabilitySink &sink, const AstNode &node,
    observability::ObservationCode code, std::string message,
    std::string category = {}) {
  observability::Observation observation{
      .severity = observability::ObservationSeverity::Error,
      .code = code,
      .message = std::move(message),
      .category = std::move(category)};
  if (const auto *document = tryGetDocument(node); document != nullptr) {
    observation.uri = document->uri;
    observation.languageId = document->textDocument().languageId();
    observation.documentId = document->id;
    observation.state = document->state;
  }
  sink.publish(observation);
}

[[nodiscard]] ValidationPreparation wrap_validation_preparation(
    ValidationPreparation check,
    const std::shared_ptr<observability::ObservabilitySink> &sink,
    observability::ObservationCode code, std::string errorPrefix,
    std::string unknownErrorMessage) {
  return [check = std::move(check), sink, code,
          errorPrefix = std::move(errorPrefix),
          unknownErrorMessage = std::move(unknownErrorMessage)](
             const AstNode &rootNode, const ValidationAcceptor &acceptor,
             std::span<const std::string> categories,
             const utils::CancellationToken &cancelToken) {
    try {
      check(rootNode, acceptor, categories, cancelToken);
    } catch (const utils::OperationCancelled &) {
      throw;
    } catch (const std::exception &error) {
      auto message = errorPrefix + error.what();
      publish_validation_observation(*sink, rootNode, code, message);
      acceptor.error(rootNode, message);
    } catch (...) {
      publish_validation_observation(*sink, rootNode, code, unknownErrorMessage);
      acceptor.error(rootNode, unknownErrorMessage);
    }
  };
}

} // namespace

void DefaultValidationRegistry::validate_category(std::string_view category) {
  if (category == kBuiltInValidationCategory) {
    throw utils::ValidationRegistryError(
        "The 'built-in' category is reserved for parser/linking diagnostics.");
  }
}

std::shared_ptr<const detail::CompiledValidationRegistry>
DefaultValidationRegistry::compiledRegistry() const {
  if (_compileClean.load(std::memory_order_acquire)) {
    return _compiled;
  }

  std::scoped_lock lock(_compileMutex);
  if (_compileClean.load(std::memory_order_relaxed)) {
    return _compiled;
  }

  auto compiled = std::make_shared<detail::CompiledValidationRegistry>();
  compiled->knownCategories = _knownCategories;

  compiled->checks.reserve(_registeredChecks.size());
  for (const auto &entry : _registeredChecks) {
    compiled->checks.push_back(detail::CompiledValidationCheckEntry{
        .targetType = entry.targetType,
        .check = entry.check,
        .matches = entry.matches,
        .exactTypeOnly = entry.exactTypeOnly,
        .category = entry.category});
  }

  const auto &reflection = *services.shared.astReflection;
  for (const auto &entry : compiled->checks) {
    // Exact-type checks run only for nodes whose dynamic type is exactly the
    // target, so index them under that one type and never expand to subtypes.
    if (entry.exactTypeOnly) {
      compiled->checksByType[entry.targetType].push_back(&entry);
      continue;
    }
    // A target the grammar never named (an abstract C++ base absent from
    // reflection) has no known concrete subtypes to index by, so `runChecks`
    // would never reach it via the dynamic-type lookup. Dispatch it through the
    // fallback list instead, gated per node by its `matches` predicate — the
    // dynamic_cast fallback the reflection's `isKnown` contract prescribes.
    if (!reflection.isKnown(entry.targetType)) {
      if (entry.matches != nullptr) {
        compiled->fallbackChecks.push_back(&entry);
      }
      continue;
    }
    compiled->checksByType[entry.targetType].push_back(&entry);
    for (const auto subtype : reflection.getAllSubTypes(entry.targetType)) {
      if (subtype == entry.targetType) {
        continue;
      }
      compiled->checksByType[subtype].push_back(&entry);
    }
  }

  compiled->sink = services.shared.observabilitySink;

  _compiled = std::move(compiled);
  _compileClean.store(true, std::memory_order_release);

  return _compiled;
}

void DefaultValidationRegistry::registerTypedCheck(
    ValidationCheckRegistration registration, std::string_view category) {
  if (!registration.check || category.empty()) {
    return;
  }

  validate_category(category);

  const std::string storedCategory(category);
  _registeredChecks.push_back(RegisteredValidationCheckEntry{
      .targetType = registration.targetType,
      .check = std::move(registration.check),
      .matches = registration.matches,
      .exactTypeOnly = registration.exactTypeOnly,
      .category = storedCategory});
  if (std::ranges::find(_knownCategories, storedCategory) ==
      _knownCategories.end()) {
    _knownCategories.push_back(storedCategory);
  }
  _compileClean.store(false, std::memory_order_relaxed);
}

void DefaultValidationRegistry::runChecks(
    const AstNode &node, const ValidationAcceptor &acceptor,
    std::span<const std::string> categories,
    const utils::CancellationToken &cancelToken) const {
  // Publish the compiled snapshot exactly once (compiledRegistry() is
  // thread-safe), then read it lock-free. Registration — the only mutation — is
  // done before any build, so `_compiled` is stable across the parallel
  // validation phase and safe to dereference without a per-node refcount bump.
  if (!_compileClean.load(std::memory_order_acquire)) {
    (void)compiledRegistry();
  }
  const auto &compiled = *_compiled;
  const auto checksIt =
      compiled.checksByType.find(std::type_index(typeid(node)));
  const bool hasTypeChecks = checksIt != compiled.checksByType.end();
  if (!hasTypeChecks && compiled.fallbackChecks.empty()) {
    return;
  }

  // An empty `categories` span enables every category; otherwise a check runs
  // only when its category was requested. Testing membership against the small
  // `categories` span per call keeps the registry free of shared mutable state,
  // so it is safe to share across documents validated in parallel.
  const bool allCategories = categories.empty();

  const auto runEntry =
      [&](const detail::CompiledValidationCheckEntry *entry) {
        utils::throw_if_cancelled(cancelToken);
        if (!allCategories &&
            std::ranges::find(categories, entry->category) ==
                categories.end()) {
          return;
        }
        try {
          entry->check(node, acceptor, cancelToken);
        } catch (const utils::OperationCancelled &) {
          throw;
        } catch (const std::exception &error) {
          auto message = std::string("An error occurred during validation: ") +
                         error.what();
          publish_validation_observation(
              *compiled.sink, node,
              observability::ObservationCode::ValidationCheckThrew, message,
              entry->category);
          acceptor.error(node, message);
        } catch (...) {
          const auto message =
              std::string("An unknown error occurred during validation.");
          publish_validation_observation(
              *compiled.sink, node,
              observability::ObservationCode::ValidationCheckThrew, message,
              entry->category);
          acceptor.error(node, message);
        }
      };

  if (hasTypeChecks) {
    for (const auto *entry : checksIt->second) {
      runEntry(entry);
    }
  }
  // Checks whose target is an abstract base absent from reflection could not be
  // pre-indexed by concrete type; gate each by a runtime is-instance test.
  for (const auto *entry : compiled.fallbackChecks) {
    if (entry->matches != nullptr && entry->matches(node)) {
      runEntry(entry);
    }
  }
}

std::span<const ValidationPreparation>
DefaultValidationRegistry::checksBefore() const noexcept {
  return _registeredChecksBefore;
}

std::span<const ValidationPreparation>
DefaultValidationRegistry::checksAfter() const noexcept {
  return _registeredChecksAfter;
}

std::vector<std::string>
DefaultValidationRegistry::getAllValidationCategories() const {
  return compiledRegistry()->knownCategories;
}

void DefaultValidationRegistry::registerBeforeDocument(
    ValidationPreparation check) {
  if (!check) {
    return;
  }

  _registeredChecksBefore.push_back(wrap_validation_preparation(
      std::move(check), services.shared.observabilitySink,
      observability::ObservationCode::ValidationPreparationThrew,
      "An error occurred during set-up of the validation: ",
      "An error occurred during set-up of the validation"));
  _compileClean.store(false, std::memory_order_relaxed);
}

void DefaultValidationRegistry::registerAfterDocument(
    ValidationPreparation check) {
  if (!check) {
    return;
  }

  _registeredChecksAfter.push_back(wrap_validation_preparation(
      std::move(check), services.shared.observabilitySink,
      observability::ObservationCode::ValidationFinalizationThrew,
      "An error occurred during tear-down of the validation: ",
      "An error occurred during tear-down of the validation"));
  _compileClean.store(false, std::memory_order_relaxed);
}

} // namespace pegium::validation
