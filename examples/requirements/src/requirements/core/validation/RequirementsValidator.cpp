#include "requirements/core/validation/RequirementsValidator.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"

namespace req {

namespace {

bool has_digit(std::string_view value) {
  return std::ranges::any_of(
      value, [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

RequirementsValidator::RequirementsValidator(
    const cursiv::CoreServices& services)
    : cursiv::DefaultCoreService(services),
      _owner(services.shared),
      _covered(services.shared) {}

void RequirementsValidator::checkRequirementNameContainsANumber(
    const ast::Requirement& requirement,
    cursiv::ValidationAcceptor& accept) const {
  if (!has_digit(requirement.name))
    accept
        .warning(requirement, "Requirement name "
                                  + std::string(requirement.name)
                                  + " should contain a number.")
        .property<&ast::Requirement::name>();
}

std::shared_ptr<const RequirementsValidator::Covered>
RequirementsValidator::computeCoverage() const {
  const auto& documents = *services.shared.documents;
  auto referenced = std::make_shared<Covered>();
  for (const std::shared_ptr<cursiv::Document>& document : documents.all()) {
    const cursiv::AstNode* const root = document->parseResult.value;
    if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::TestModel>)
      continue;
    for (const ast::Test* const test :
         static_cast<const ast::TestModel*>(root)->tests) {
      if (test == nullptr) continue;
      for (const auto& reference : test->requirements)
        if (const ast::Requirement* const target = reference.get())
          referenced->insert(target);
    }
  }
  return referenced;
}

void RequirementsValidator::checkRequirementIsCoveredByATest(
    const ast::Requirement& requirement,
    cursiv::ValidationAcceptor& accept) const {
  const Covered* const covered = _covered.get(0, [&]() -> const Covered* {
    // Only misses wait; this scan never calls back into the validator.
    const std::lock_guard lock(_coverageMutex);
    return _owner.get(0, [this] { return computeCoverage(); }).get();
  });

  if (covered != nullptr && !covered->contains(&requirement))
    accept.warning(requirement, "Requirement " + std::string(requirement.name)
                                    + " not covered by a test.");
}

}  // namespace req
