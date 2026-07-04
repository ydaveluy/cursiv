#pragma once

#include <memory>
#include <string>

#include <statemachine/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace statemachine {

/// Creates a standalone statemachine parser.
std::unique_ptr<const pegium::parser::Parser> createStatemachineParser();

/// Wires the statemachine core overrides. The LSP path reuses it directly.
void installStatemachineCoreModule(StatemachineCoreServices &services);

/// Builds the core-only statemachine language services.
std::unique_ptr<StatemachineCoreServices>
createStatemachineCoreServices(
    const StatemachineSharedCoreServices &sharedServices,
    std::string languageId = "statemachine");

/// Registers the core-only statemachine services in `sharedServices`.
bool registerStatemachineCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace statemachine
