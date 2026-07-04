#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <typeindex>
#include <vector>

#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace pegium {
struct AstNode;
} // namespace pegium

namespace pegium::validation {

class ValidationAcceptor;

namespace detail {
struct CompiledValidationRegistry;
} // namespace detail

/// Default mutable registry of validation checks and preparations.
///
/// Adds no public API beyond the abstract `ValidationRegistry`: every
/// implementation detail (the compiled registry snapshot built lazily under
/// `_compileMutex`) is private. Tests interact through the abstract interface.
class DefaultValidationRegistry : public ValidationRegistry,
                                  protected pegium::DefaultCoreService {
public:
  using ValidationRegistry::registerCheck;
  using pegium::DefaultCoreService::DefaultCoreService;

  // ValidationRegistry overrides:
  [[nodiscard]] std::vector<std::string>
  getAllValidationCategories() const override;
  void registerBeforeDocument(ValidationPreparation check) override;
  void registerAfterDocument(ValidationPreparation check) override;
  [[nodiscard]] std::span<const ValidationPreparation>
  checksBefore() const noexcept override;
  [[nodiscard]] std::span<const ValidationPreparation>
  checksAfter() const noexcept override;
  void runChecks(const AstNode &node, const ValidationAcceptor &acceptor,
                 std::span<const std::string> categories,
                 const utils::CancellationToken &cancelToken) const override;

private:
  struct RegisteredValidationCheckEntry {
    std::type_index targetType = std::type_index(typeid(AstNode));
    ValidationCheck check;
    ValidationCheckMatches matches = nullptr;
    bool exactTypeOnly = false;
    std::string category;
  };

  static void validate_category(std::string_view category);

  /// Returns the compiled snapshot, building it on first use. Thread-safe: the
  /// snapshot is published once under `_compileMutex` and then read lock-free
  /// during validation (registration, which invalidates it, only runs before a
  /// build starts).
  [[nodiscard]] std::shared_ptr<const detail::CompiledValidationRegistry>
  compiledRegistry() const;

  void registerTypedCheck(ValidationCheckRegistration registration,
                          std::string_view category) override;

  mutable std::vector<RegisteredValidationCheckEntry> _registeredChecks;
  mutable std::vector<ValidationPreparation> _registeredChecksBefore;
  mutable std::vector<ValidationPreparation> _registeredChecksAfter;
  mutable std::vector<std::string> _knownCategories{
      std::string(kFastValidationCategory),
      std::string(kSlowValidationCategory),
      std::string(kBuiltInValidationCategory)};
  mutable std::mutex _compileMutex;
  mutable std::shared_ptr<const detail::CompiledValidationRegistry> _compiled;
  // True once `_compiled` is built and current; stored with release in
  // compiledRegistry() and loaded with acquire so validation can dereference
  // `_compiled` lock-free across parallel documents.
  mutable std::atomic<bool> _compileClean{false};
};

} // namespace pegium::validation
