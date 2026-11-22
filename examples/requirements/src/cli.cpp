// The requirements command line: the usual check, plus `generate FILE`: an
// HTML coverage summary, which needs both languages of the workspace.

#include "cursiv/cli.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "requirements/CoreModule.hpp"
#include "requirements/Generator.hpp"

namespace {

int generate(const cursiv::Document& document, std::string_view path,
             std::optional<std::string_view> destination) {
  const auto* const model = cursiv::ast_cast<const req::ast::RequirementModel>(
      document.parseResult.value);
  std::vector<const req::ast::TestModel*> tests;
  bool errors = false;
  for (const auto& other : document.language->shared().documents->all()) {
    if (other.get() != &document
        && other->textDocument().languageId()
               != req::generated::Tests::kLanguageId)
      continue;
    if (const auto* test = cursiv::ast_cast<const req::ast::TestModel>(
            other->parseResult.value))
      tests.push_back(test);
    if (!cursiv::hasErrorDiagnostics(*other)) continue;
    if (!errors) std::cerr << "There are validation errors:\n";
    errors = true;
    cursiv::printDiagnostics(
        *other, cursiv::fileUriToPath(other->uri).value_or(other->uri),
        std::cerr);
  }
  if (errors) return cursiv::kDiagnostics;
  if (!model) {
    std::cerr
        << "Unable to generate a summary for an invalid requirement model.\n";
    return cursiv::kDiagnostics;
  }

  // Written first: a summary that cannot be written is not reported.
  const std::string summary =
      req::generate_summary(*model, tests, path, destination);
  std::cout << "Requirement coverage generated successfully: " << summary
            << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  return cursiv::runCli<req::RequirementsSharedCoreServices>(
      argc, argv, &req::register_core_services,
      {{.name = "generate",
        .run = generate,
        .acceptsDestination = true,
        .loadSiblings = true}});
}
