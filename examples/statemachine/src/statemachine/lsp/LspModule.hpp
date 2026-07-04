#pragma once

#include <memory>
#include <string>

#include <statemachine/lsp/LspServices.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace statemachine {

/// LSP service overrides applied on top of pegium's default LSP services.
void installStatemachineLspModule(StatemachineServices &services);

/// Builds the LSP-enabled statemachine language services.
std::unique_ptr<StatemachineServices>
createStatemachineLspServices(const StatemachineSharedServices &sharedServices,
                              std::string languageId = "statemachine");

/// Registers the LSP-enabled statemachine services in `sharedServices`.
bool registerStatemachineLspServices(pegium::SharedServices &sharedServices);

} // namespace statemachine
