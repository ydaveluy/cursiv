#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <pegium/core/documentation/CommentProvider.hpp>
#include <pegium/core/documentation/DocumentationProvider.hpp>
#include <pegium/core/parser/Parser.hpp>
#include <pegium/core/references/Linker.hpp>
#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/references/References.hpp>
#include <pegium/core/references/ScopeComputation.hpp>
#include <pegium/core/references/ScopeProvider.hpp>
#include <pegium/core/validation/DocumentValidator.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>
#include <pegium/core/workspace/AstNodeDescriptionProvider.hpp>
#include <pegium/core/workspace/ReferenceDescriptionProvider.hpp>

namespace pegium {

struct SharedCoreServices;

/// Language-level metadata used for URI-to-language resolution.
struct LanguageMetaData {
  std::string languageId;
  std::vector<std::string> fileExtensions;
  std::vector<std::string> fileNames;
};

/// Documentation-related services owned by one language; installed by default.
struct DocumentationServices {
  std::unique_ptr<documentation::CommentProvider> commentProvider;
  std::unique_ptr<documentation::DocumentationProvider> documentationProvider;
};

/// Cross-reference services owned by one language; installed by default.
struct ReferenceServices {
  std::unique_ptr<references::NameProvider> nameProvider;
  std::unique_ptr<references::ScopeProvider> scopeProvider;
  std::unique_ptr<references::References> references;
  std::unique_ptr<references::ScopeComputation> scopeComputation;
  std::unique_ptr<references::Linker> linker;
};

/// Validation services owned by one language; installed by default.
struct ValidationServices {
  std::unique_ptr<validation::ValidationRegistry> validationRegistry;
  std::unique_ptr<validation::DocumentValidator> documentValidator;
};

/// Workspace-facing services owned by one language; installed by default.
struct WorkspaceServices {
  std::unique_ptr<workspace::AstNodeDescriptionProvider>
      astNodeDescriptionProvider;
  std::unique_ptr<workspace::ReferenceDescriptionProvider>
      referenceDescriptionProvider;
};

/// Root service container for one language configuration.
struct CoreServices {
  explicit CoreServices(const SharedCoreServices &sharedServices)
      : shared(sharedServices) {}
  // Non-movable: installed default services hold a back-reference to this
  // CoreServices (DefaultCoreService::services), so a member-wise move would
  // leave them dangling. CoreServices is only ever heap-owned via unique_ptr,
  // so the move ctor is never needed.
  CoreServices(CoreServices &&) noexcept = delete;
  CoreServices &operator=(CoreServices &&) noexcept = delete;
  CoreServices(const CoreServices &) = delete;
  CoreServices &operator=(const CoreServices &) = delete;
  virtual ~CoreServices() noexcept = default;

  /// Returns the field path of the first required service that is missing
  /// (e.g. `"parser"` or `"references.scopeProvider"`), or `std::nullopt` when
  /// the container is complete. This is the single source of truth for the
  /// required-service set; `isComplete()` is derived from it.
  [[nodiscard]] virtual std::optional<std::string_view>
  firstMissingService() const noexcept;

  [[nodiscard]] virtual bool isComplete() const noexcept;

  LanguageMetaData languageMetaData;
  const SharedCoreServices &shared;

  DocumentationServices documentation;
  // Required for a complete language service, but not installed by
  // installDefaultCoreServices(...).
  std::unique_ptr<const parser::Parser> parser;
  ReferenceServices references;
  ValidationServices validation;
  WorkspaceServices workspace;
};

/// Core container bound to a language's own shared-core type.
///
/// Derives `CoreServices` virtually — so it can also be the core base of an LSP
/// container through the diamond — and re-exposes `shared` typed to `TShared`,
/// shadowing `CoreServices::shared`. A language derives this and adds its
/// members; it must still spell out the constructor, because an inherited
/// constructor can never initialize the virtual `CoreServices` base:
///
/// ```cpp
/// struct MyCoreServices : pegium::CoreServicesFor<MySharedCoreServices> {
///   std::unique_ptr<MyValidator> validator;
///   explicit MyCoreServices(const MySharedCoreServices &sharedServices)
///       : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
/// };
/// ```
template <typename TShared> struct CoreServicesFor : virtual CoreServices {
  static_assert(std::is_base_of_v<SharedCoreServices, TShared>,
                "TShared must derive from pegium::SharedCoreServices.");

  const TShared &shared;

  explicit CoreServicesFor(const TShared &sharedServices)
      : CoreServices(sharedServices), shared(sharedServices) {}
};

/// Installs the default language-level services into `services`.
///
/// `installDefaultSharedCoreServices(...)` must already have been called on
/// `services.shared`.
void installDefaultCoreServices(CoreServices &services);

/// Builds one core-only language service container with the default core
/// services installed — the headless counterpart of `makeDefaultServices`.
/// `TShared` is deduced from `sharedServices`, so passing a custom
/// `SharedCoreServices` subclass constructs the container from that exact type —
/// its constructor can take the custom shared reference directly.
template <typename TServices = CoreServices, typename TShared>
[[nodiscard]] std::unique_ptr<TServices>
makeDefaultCoreServices(const TShared &sharedServices, std::string languageId) {
  static_assert(std::is_base_of_v<CoreServices, TServices>,
                "TServices must derive from pegium::CoreServices.");
  static_assert(std::is_base_of_v<SharedCoreServices, TShared>,
                "TShared must derive from pegium::SharedCoreServices.");
  static_assert(std::is_constructible_v<TServices, const TShared &>,
                "TServices must be constructible from const TShared&.");

  auto services = std::make_unique<TServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  installDefaultCoreServices(*services);
  return services;
}

} // namespace pegium
