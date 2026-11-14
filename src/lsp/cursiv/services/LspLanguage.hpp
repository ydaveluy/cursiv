#pragma once

#include <memory>
#include <utility>

#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"

// How a language is built and registered for the language server: as in
// `CoreLanguage.hpp`, with the LSP defaults and the LSP module on top.

namespace cursiv {

// The container of one language: the core defaults and module, then the LSP
// defaults and module.
template <class TServices, class TShared, auto installCore, auto installLsp>
[[nodiscard]] std::unique_ptr<TServices> create_lsp_services(
    TShared& sharedServices) {
  auto services = std::make_unique<TServices>(sharedServices);
  install_default_core_services(*services);
  installCore(*services);
  install_default_lsp_services(*services);
  installLsp(*services);
  return services;
}

// A new workspace with this one language; without `files` it gets the empty
// file system provider.
template <class TServices, class TShared, auto installCore, auto installLsp>
[[nodiscard]] CoreLanguage<TServices, TShared> create_lsp_language(
    std::unique_ptr<FileSystemProvider> files = nullptr) {
  auto shared = std::make_unique<TShared>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<EmptyFileSystemProvider>();
  install_default_shared_lsp_services(*shared);
  auto services =
      create_lsp_services<TServices, TShared, installCore, installLsp>(*shared);
  TServices& language = *services;
  shared->serviceRegistry->registerServices(std::move(services));
  return {std::move(shared), language};
}

// Registers the language in an existing workspace, whose shared services must
// be a `TShared`.
template <class TServices, class TShared, auto installCore, auto installLsp>
void register_lsp_language(SharedCoreServices& sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      create_lsp_services<TServices, TShared, installCore, installLsp>(
          dynamic_cast<TShared&>(sharedServices)));
}

}  // namespace cursiv
