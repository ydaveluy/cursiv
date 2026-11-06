#include "requirements/validation/TestsValidator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace req {

namespace {

bool has_digit(std::string_view value) {
  return std::ranges::any_of(
      value, [](unsigned char c) { return std::isdigit(c) != 0; });
}

// A test naming a handful of environments asks its requirements once per
// environment. Past that, the environments the requirements use are gathered
// once, so a long list costs one pass over them and not one per entry.
constexpr std::size_t kAskedDirectly = 8;

bool used_by_a_requirement(const ast::Test& test,
                           const ast::Environment* environment) {
  for (const auto& reference : test.requirements)
    if (const ast::Requirement* const requirement = reference.get())
      for (const auto& required : requirement->environments)
        if (required.get() == environment) return true;
  return false;
}

std::unordered_set<const ast::Environment*> environments_used_by(
    const ast::Test& test) {
  std::unordered_set<const ast::Environment*> used;
  for (const auto& reference : test.requirements)
    if (const ast::Requirement* const requirement = reference.get())
      for (const auto& required : requirement->environments)
        used.insert(required.get());
  return used;
}

}  // namespace

void TestsValidator::checkTestNameContainsANumber(
    const ast::Test& test, cursiv::ValidationAcceptor& accept) const {
  if (!has_digit(test.name))
    accept
        .warning(test, "Test name " + std::string(test.name)
                           + " should contain a number.")
        .property<&ast::Test::name>();
}

void TestsValidator::
    checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement(
        const ast::Test& test, cursiv::ValidationAcceptor& accept) const {
  const auto warn = [&](std::size_t at, const ast::Environment& environment) {
    accept
        .warning(test, "Test " + std::string(test.name) +
                           " references environment " +
                           std::string(environment.name) +
                           " which is not used by any referenced "
                           "requirement.")
        .property<&ast::Test::environments>(at);
  };

  if (test.environments.size() > kAskedDirectly) {
    const auto used = environments_used_by(test);
    for (std::size_t at = 0; at < test.environments.size(); ++at)
      if (const ast::Environment* const environment =
              test.environments[at].get();
          environment != nullptr && !used.contains(environment))
        warn(at, *environment);
    return;
  }
  for (std::size_t at = 0; at < test.environments.size(); ++at)
    if (const ast::Environment* const environment = test.environments[at].get();
        environment != nullptr && !used_by_a_requirement(test, environment))
      warn(at, *environment);
}

}  // namespace req
