#pragma once

#include <memory>
#include <string>

#include <arithmetics/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace arithmetics {

/// Creates a standalone arithmetics parser.
std::unique_ptr<const pegium::parser::Parser> createArithmeticsParser();

/// Wires the arithmetics core overrides onto a service container.
///
/// Takes the pegium core base and the arithmetics graft as two separate
/// references, so it is a plain function rather than a template: every container
/// is-a `pegium::CoreServices` and is-a `ArithmeticsAddedServices`, so the
/// headless and the LSP bundle both wire themselves with
/// `installArithmeticsCoreModule(*services, *services)`.
void installArithmeticsCoreModule(pegium::CoreServices &core,
                                  ArithmeticsAddedServices &added);

/// Builds the core-only arithmetics language services.
std::unique_ptr<ArithmeticsCoreServices>
createArithmeticsCoreServices(const pegium::SharedCoreServices &sharedServices,
                              std::string languageId = "arithmetics");

/// Registers the core-only arithmetics services in `sharedServices`.
bool registerArithmeticsCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace arithmetics
