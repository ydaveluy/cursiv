#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>

#include <pegium/cli/CliUtils.hpp>

#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: @PEGIUM_NEW_LANGUAGE_ID@-cli <file@PEGIUM_NEW_EXT@>\n";
    return 1;
  }
  try {
    auto sharedServices = pegium::make_shared_services<
        @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedCoreServices>();
    auto &shared = *sharedServices;
    auto services = @PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@CoreServices(shared);
    auto &langServices = *services;
    shared.serviceRegistry->registerServices(std::move(services));

    auto document =
        pegium::build_document_from_path(argv[1], langServices);
    if (pegium::has_error_diagnostics(*document)) {
      std::cerr << "Validation errors:\n";
      pegium::print_error_diagnostics(*document, std::cerr);
      return 2;
    }
    std::cout << "Parsed " << argv[1] << " successfully.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 3;
  }
}
