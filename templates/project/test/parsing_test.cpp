#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreModule.hpp>
#include <pegium/cli/CliUtils.hpp>

#include <gtest/gtest.h>

TEST(@PEGIUM_NEW_CLASS@Parsing, SampleParsesWithoutErrors) {
  auto sharedServices = pegium::make_shared_services<
      @PEGIUM_NEW_LANGUAGE_ID@::@PEGIUM_NEW_CLASS@SharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = @PEGIUM_NEW_LANGUAGE_ID@::create@PEGIUM_NEW_CLASS@CoreServices(shared);
  auto &langServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));

  auto document = pegium::build_document_from_path(
      PEGIUM_NEW_SAMPLE_PATH, langServices);
  EXPECT_FALSE(pegium::has_error_diagnostics(*document));
}
