#pragma once

#include <concepts>
#include <iosfwd>
#include <memory>
#include <string_view>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace pegium::workspace {
struct Document;
}

namespace pegium {

namespace detail {
/// Installs the default shared core services and readies the standalone
/// configuration provider on `sharedServices`. Split out so the templated
/// `make_shared_services` stays a thin, container-type-only wrapper.
void initializeSharedServicesForCli(SharedCoreServices &sharedServices);
} // namespace detail

/// Creates shared services for standalone CLI execution.
///
/// `T` is the shared-service container (default `pegium::SharedCoreServices`); a
/// language may pass its own subclass to add workspace-wide shared services (it
/// must be default-constructible). Returns an owning handle (SharedCoreServices
/// is non-movable: its installed services hold back-references to it, so it must
/// stay at a fixed address).
template <typename T = SharedCoreServices>
  requires std::derived_from<T, SharedCoreServices>
[[nodiscard]] std::unique_ptr<T> make_shared_services() {
  auto sharedServices = std::make_unique<T>();
  detail::initializeSharedServicesForCli(*sharedServices);
  return sharedServices;
}

/// Loads or rebuilds the document for `path` and returns the shared snapshot.
[[nodiscard]] std::shared_ptr<workspace::Document>
build_document_from_path(std::string_view path,
                         const pegium::CoreServices &services,
                         bool validation = true);

/// Returns whether `document` currently contains error diagnostics.
[[nodiscard]] bool
has_error_diagnostics(const workspace::Document &document) noexcept;

/// Prints error diagnostics from `document` to `out`.
void print_error_diagnostics(const workspace::Document &document,
                             std::ostream &out);

} // namespace pegium
