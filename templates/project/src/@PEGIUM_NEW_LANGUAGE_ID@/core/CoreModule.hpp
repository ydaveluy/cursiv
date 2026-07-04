#pragma once

#include <memory>
#include <string>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Creates a standalone @PEGIUM_NEW_CLASS@ parser.
std::unique_ptr<const pegium::parser::Parser> create@PEGIUM_NEW_CLASS@Parser();

/// Wires the @PEGIUM_NEW_CLASS@ core overrides. The LSP path reuses it directly.
void install@PEGIUM_NEW_CLASS@CoreModule(@PEGIUM_NEW_CLASS@CoreServices &services);

/// Builds the core-only @PEGIUM_NEW_CLASS@ language services.
std::unique_ptr<@PEGIUM_NEW_CLASS@CoreServices>
create@PEGIUM_NEW_CLASS@CoreServices(
    const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices,
    std::string languageId = "@PEGIUM_NEW_LANGUAGE_ID@");

/// Registers the core-only @PEGIUM_NEW_CLASS@ services in `sharedServices`.
bool register@PEGIUM_NEW_CLASS@CoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
