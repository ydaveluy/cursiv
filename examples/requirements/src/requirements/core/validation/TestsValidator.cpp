#include "requirements/core/validation/TestsValidator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace req {

namespace {

bool has_digit(std::string_view value) {
  return std::ranges::any_of(
      value, [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

void TestsValidator::checkTestNameContainsANumber(
    const ast::Test& test, cursiv::ValidationAcceptor& accept) const {
  if (!has_digit(test.name))
    accept
        .warning(test, "Test name " + std::string(test.name) +
                           " should contain a number.")
        .property<&ast::Test::name>();
}

void TestsValidator::
    checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement(
        const ast::Test& test, cursiv::ValidationAcceptor& accept) const {
  for (std::size_t at = 0; at < test.environments.size(); ++at) {
    const ast::Environment* const environment = test.environments[at].get();
    if (environment == nullptr) continue;

    bool used = false;
    for (const auto& reference : test.requirements) {
      const ast::Requirement* const requirement = reference.get();
      if (requirement == nullptr) continue;
      for (const auto& required : requirement->environments)
        if (required.get() == environment) {
          used = true;
          break;
        }
      if (used) break;
    }

    if (!used)
      accept
          .warning(test, "Test " + std::string(test.name) +
                             " references environment " +
                             std::string(environment->name) +
                             " which is not used by any referenced "
                             "requirement.")
          .property<&ast::Test::environments>(at);
  }
}

}  // namespace req
