#pragma once

#include <memory>
#include <utility>

#include "cursiv/services/CoreServicesFor.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"

// How a language is built and registered: only the two containers and the
// module that fills them differ between languages.

namespace cursiv {

// A workspace holding one language. The reference is into the registry that
// `shared` owns: it dies with it.
template <class TServices, class TShared>
struct CoreLanguage {
  std::unique_ptr<TShared> shared;
  TServices& language;
};

// The container of one language: the defaults, then the module. The module
// names the language -- its metadata comes from the grammar, not from here.
template <class TServices, class TShared, void (*install)(TServices&)>
[[nodiscard]] std::unique_ptr<TServices> create_core_services(
    TShared& sharedServices) {
  auto services = make_default_core_services<TServices>(sharedServices, {});
  install(*services);
  return services;
}

// A new workspace with this one language; without `files` it gets the empty
// file system provider.
template <class TServices, class TShared, void (*install)(TServices&)>
[[nodiscard]] CoreLanguage<TServices, TShared> create_language(
    std::unique_ptr<FileSystemProvider> files = nullptr) {
  auto shared = std::make_unique<TShared>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<EmptyFileSystemProvider>();
  install_default_shared_core_services(*shared);
  auto services = create_core_services<TServices, TShared, install>(*shared);
  TServices& language = *services;
  shared->serviceRegistry->registerServices(std::move(services));
  return {std::move(shared), language};
}

// Registers the language in an existing workspace, whose shared services must
// be a `TShared`.
template <class TServices, class TShared, void (*install)(TServices&)>
void register_language(SharedCoreServices& sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      create_core_services<TServices, TShared, install>(
          dynamic_cast<TShared&>(sharedServices)));
}

}  // namespace cursiv
