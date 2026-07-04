#pragma once

#include <memory>
#include <string>

#include <arithmetics/lsp/LspServices.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace arithmetics {

/// LSP service overrides applied on top of pegium's default LSP services.
void installArithmeticsLspModule(ArithmeticsServices &services);

/// Builds the LSP-enabled arithmetics language services.
std::unique_ptr<ArithmeticsServices>
createArithmeticsLspServices(const ArithmeticsSharedServices &sharedServices,
                             std::string languageId = "arithmetics");

/// Registers the LSP-enabled arithmetics services in `sharedServices`.
bool registerArithmeticsLspServices(pegium::SharedServices &sharedServices);

} // namespace arithmetics
