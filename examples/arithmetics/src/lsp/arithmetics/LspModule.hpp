#pragma once

#include <memory>
#include <string>

#include "arithmetics/LspServices.hpp"
#include "arithmetics/generated/Arith/Module.hpp"
#include "cursiv/services/LspLanguage.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"

namespace arith {

void install_lsp_module(ArithmeticsServices& services);

// The shared services and the language, created in one call.
using ArithmeticsLanguage =
    cursiv::CoreLanguage<ArithmeticsServices, ArithmeticsSharedServices>;

ArithmeticsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

void register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace arith
