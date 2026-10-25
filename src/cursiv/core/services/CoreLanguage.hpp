#pragma once

#include <memory>
#include <utility>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"

// How a language is built and filed, written once: the two containers and the
// module that fills them are all that separates one language from another.

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

// A workspace of its own with this one language in it; no `files` is no disk
// under it, never an absent provider.
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

// The same language in a workspace someone else owns, which is this language's
// own shared type: recovered by reference so the container is built with the
// shared services it expects.
template <class TServices, class TShared, void (*install)(TServices&)>
bool register_language(SharedCoreServices& sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      create_core_services<TServices, TShared, install>(
          dynamic_cast<TShared&>(sharedServices)));
  return true;
}

}  // namespace cursiv
