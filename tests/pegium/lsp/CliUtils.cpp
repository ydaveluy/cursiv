#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <pegium/core/CoreTestSupport.hpp>
#include <pegium/cli/CliUtils.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/utils/UriUtils.hpp>

namespace pegium {
namespace {

struct RegisteredLanguage {
  pegium::CoreServices *services = nullptr;
  test::FakeParser *parser = nullptr;
  test::FakeDocumentValidator *validator = nullptr;
};

std::filesystem::path make_temp_directory() {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  auto path = std::filesystem::temp_directory_path() /
              ("pegium-cli-tests-" + suffix);
  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path write_file(const std::filesystem::path &path,
                                 std::string_view content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
  return path;
}

RegisteredLanguage register_language(
    pegium::SharedCoreServices &shared, std::string languageId,
    std::vector<std::string> fileExtensions = {},
    std::vector<std::string> fileNames = {}) {
  auto languageServices = std::make_unique<pegium::CoreServices>(shared);
  pegium::installDefaultCoreServices(*languageServices);

  auto parser = std::make_unique<test::FakeParser>();
  auto *parserPtr = parser.get();
  languageServices->parser = std::move(parser);

  auto validator = std::make_unique<test::FakeDocumentValidator>();
  auto *validatorPtr = validator.get();
  languageServices->validation.documentValidator = std::move(validator);

  languageServices->languageMetaData.languageId = std::move(languageId);
  languageServices->languageMetaData.fileExtensions = std::move(fileExtensions);
  languageServices->languageMetaData.fileNames = std::move(fileNames);

  auto *servicesPtr = languageServices.get();
  shared.serviceRegistry->registerServices(std::move(languageServices));
  return {
      .services = servicesPtr,
      .parser = parserPtr,
      .validator = validatorPtr,
  };
}

TEST(CliUtilsTest, MakeSharedServicesReadiesStandaloneConfigurationProvider) {
  auto sharedServices = pegium::make_shared_services();
  auto &shared = *sharedServices;

  ASSERT_NE(shared.workspace.configurationProvider, nullptr);
  EXPECT_TRUE(shared.workspace.configurationProvider->isReady());
}

// A language extends the shared core container with its own workspace-wide
// services; make_shared_services<T> must construct that subtype and still
// install the defaults on its base.
struct ExtendedSharedCoreServices : pegium::SharedCoreServices {
  int extraSharedService = 42;
};

TEST(CliUtilsTest, MakeSharedServicesConstructsSubtypeAndInstallsDefaults) {
  std::unique_ptr<ExtendedSharedCoreServices> sharedServices =
      pegium::make_shared_services<ExtendedSharedCoreServices>();
  ASSERT_NE(sharedServices, nullptr);

  // The language's own shared member survives construction...
  EXPECT_EQ(sharedServices->extraSharedService, 42);
  // ...and the default shared core services were installed on the subtype.
  ASSERT_NE(sharedServices->workspace.configurationProvider, nullptr);
  EXPECT_TRUE(sharedServices->workspace.configurationProvider->isReady());
}

TEST(CliUtilsTest, BuildDocumentFromPathReusesExistingDocumentForSameUri) {
  auto sharedServices = pegium::make_shared_services();
  auto &shared = *sharedServices;
  const auto language =
      register_language(shared, "cli-language", {".cli"});

  const auto tempDirectory = make_temp_directory();
  const auto inputPath =
      write_file(tempDirectory / "demo.cli", "content");

  const auto firstDocument =
      pegium::build_document_from_path(inputPath.string(), *language.services);
  const auto secondDocument =
      pegium::build_document_from_path(inputPath.string(), *language.services);

  ASSERT_NE(firstDocument, nullptr);
  ASSERT_NE(secondDocument, nullptr);
  EXPECT_EQ(firstDocument, secondDocument);
  EXPECT_EQ(language.parser->parseCalls, 1u);

  const auto uri = utils::path_to_file_uri(inputPath.string());
  EXPECT_EQ(shared.workspace.documents->getDocuments(uri).size(), 1u);

  std::filesystem::remove_all(tempDirectory);
}

TEST(CliUtilsTest, BuildDocumentFromPathRejectsBadInputsClearly) {
  struct Case {
    const char *name;
    std::vector<std::string> fileExtensions;
    std::vector<std::string> fileNames;
    const char *fileToCreate; // nullptr: do not create the file on disk.
    const char *inputFileName;
    const char *expectedMessageFragment;
  };

  static const Case kCases[] = {
      {"MissingFiles", {".cli"}, {}, nullptr, "missing.cli", "does not exist."},
      {"UnexpectedExtensions",
       {".cli"},
       {},
       "demo.txt",
       "demo.txt",
       "Please choose a file with one of these extensions: .cli."},
      {"UnexpectedFileNames",
       {},
       {"pegium.config"},
       "other.config",
       "other.config",
       "Please choose a file with one of these names: pegium.config."},
  };

  for (const auto &testCase : kCases) {
    SCOPED_TRACE(testCase.name);

    auto sharedServices = pegium::make_shared_services();
    auto &shared = *sharedServices;
    const auto language = register_language(
        shared, "cli-language", testCase.fileExtensions, testCase.fileNames);

    const auto tempDirectory = make_temp_directory();
    if (testCase.fileToCreate != nullptr) {
      write_file(tempDirectory / testCase.fileToCreate, "content");
    }
    const auto inputPath = tempDirectory / testCase.inputFileName;

    try {
      (void)pegium::build_document_from_path(inputPath.string(),
                                          *language.services);
      FAIL() << "Expected invalid_argument";
    } catch (const std::invalid_argument &error) {
      EXPECT_NE(std::string(error.what()).find(testCase.expectedMessageFragment),
                std::string::npos);
    }

    std::filesystem::remove_all(tempDirectory);
  }
}

TEST(CliUtilsTest, BuildDocumentFromPathRejectsDirectory) {
  auto sharedServices = pegium::make_shared_services();
  auto &shared = *sharedServices;
  const auto language = register_language(shared, "cli-language", {".cli"});

  const auto tempDirectory = make_temp_directory();
  // A directory whose name matches the language extension must be rejected with
  // a clear usage error, not silently parsed as an empty document.
  const auto directoryPath = tempDirectory / "demo.cli";
  std::filesystem::create_directories(directoryPath);

  try {
    (void)pegium::build_document_from_path(directoryPath.string(),
                                        *language.services);
    FAIL() << "Expected a usage error for a directory path";
  } catch (const std::invalid_argument &error) {
    EXPECT_NE(std::string(error.what()).find("is not a regular file"),
              std::string::npos);
  }

  std::filesystem::remove_all(tempDirectory);
}

TEST(CliUtilsTest, BuildDocumentFromPathRejectsEmptyPath) {
  auto sharedServices = pegium::make_shared_services();
  auto &shared = *sharedServices;
  const auto language = register_language(shared, "cli-language", {".cli"});

  try {
    (void)pegium::build_document_from_path("", *language.services);
    FAIL() << "Expected a usage error for an empty path";
  } catch (const std::invalid_argument &error) {
    EXPECT_NE(std::string(error.what()).find("No file path"),
              std::string::npos);
  }
}

TEST(CliUtilsTest, BuildDocumentFromPathSkipsValidationWhenRequested) {
  auto sharedServices = pegium::make_shared_services();
  auto &shared = *sharedServices;
  const auto language =
      register_language(shared, "cli-language", {".cli"});

  const auto tempDirectory = make_temp_directory();
  const auto inputPath =
      write_file(tempDirectory / "demo.cli", "content");

  auto document = pegium::build_document_from_path(inputPath.string(),
                                                *language.services, false);

  ASSERT_NE(document, nullptr);
  EXPECT_EQ(language.validator->validateCalls, 0u);

  std::filesystem::remove_all(tempDirectory);
}

} // namespace
} // namespace pegium
