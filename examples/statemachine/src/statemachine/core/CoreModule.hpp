#pragma once

#include <memory>
#include <string>

#include <statemachine/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace statemachine {

/// Creates a standalone statemachine parser.
std::unique_ptr<const pegium::parser::Parser> createStatemachineParser();

/// Wires the statemachine core overrides onto a service container.
///
/// Takes the pegium core base and the statemachine graft as two separate
/// references, so it is a plain function rather than a template: every container
/// is-a `pegium::CoreServices` and is-a `StatemachineAddedServices`, so the
/// headless and the LSP bundle both wire themselves with
/// `installStatemachineCoreModule(*services, *services)`.
void installStatemachineCoreModule(pegium::CoreServices &core,
                                   StatemachineAddedServices &added);

/// Builds the core-only statemachine language services.
std::unique_ptr<StatemachineCoreServices>
createStatemachineCoreServices(const pegium::SharedCoreServices &sharedServices,
                               std::string languageId = "statemachine");

/// Registers the core-only statemachine services in `sharedServices`.
bool registerStatemachineCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace statemachine
