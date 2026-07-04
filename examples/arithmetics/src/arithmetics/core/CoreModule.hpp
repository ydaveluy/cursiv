#pragma once

#include <memory>
#include <string>

#include <arithmetics/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace arithmetics {

/// Creates a standalone arithmetics parser.
std::unique_ptr<const pegium::parser::Parser> createArithmeticsParser();

/// Wires the arithmetics core overrides. The LSP path reuses it directly.
void installArithmeticsCoreModule(ArithmeticsCoreServices &services);

/// Builds the core-only arithmetics language services.
std::unique_ptr<ArithmeticsCoreServices>
createArithmeticsCoreServices(
    const ArithmeticsSharedCoreServices &sharedServices,
    std::string languageId = "arithmetics");

/// Registers the core-only arithmetics services in `sharedServices`.
bool registerArithmeticsCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace arithmetics
