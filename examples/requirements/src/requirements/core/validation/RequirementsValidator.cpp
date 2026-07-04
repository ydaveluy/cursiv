#include <requirements/core/validation/RequirementsValidator.hpp>

#include <requirements/core/ast.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>

#include <pegium/core/workspace/Documents.hpp>
#include <pegium/core/validation/DiagnosticRanges.hpp>

namespace requirements::validation {
namespace {

using namespace requirements::ast;

bool has_digit(std::string_view value) {
  return std::ranges::any_of(value, [](unsigned char c) {
    return std::isdigit(c) != 0;
  });
}

} // namespace

RequirementsValidator::RequirementsValidator(
    const pegium::CoreServices &services)
    : pegium::DefaultCoreService(services), _coveredCache(services.shared) {}

void RequirementsValidator::checkRequirementNameContainsANumber(
    const Requirement &requirement,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (!has_digit(requirement.name)) {
    accept.warning(requirement, "Requirement name " + requirement.name +
                                    " should contain a number.")
        .property<&Requirement::name>();
  }
}

void RequirementsValidator::checkRequirementIsCoveredByATest(
    const Requirement &requirement,
    const pegium::validation::ValidationAcceptor &accept,
    const pegium::workspace::Documents &documents) const {
  // Collect every requirement referenced by a test once per build (the same set
  // for every requirement), then answer each check with a constant-time lookup.
  const auto covered = _coveredCache.get(0, [&documents] {
    auto referenced = std::make_shared<CoveredRequirements>();
    for (const auto &document : documents.all()) {
      const auto *model =
          pegium::ast_ptr_cast<TestModel>(document->parseResult.value);
      if (model == nullptr) {
        continue;
      }
      for (const auto &test : model->tests) {
        if (!test) {
          continue;
        }
        for (const auto &reference : test->requirements) {
          if (const auto *target = reference.get()) {
            referenced->insert(target);
          }
        }
      }
    }
    return std::shared_ptr<const CoveredRequirements>(std::move(referenced));
  });

  if (!covered->contains(&requirement)) {
    accept.warning(requirement,
                   "Requirement " + requirement.name + " not covered by a test.");
  }
}

} // namespace requirements::validation
