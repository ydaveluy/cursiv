#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

// `.property<&T::m>()` is what puts the squiggle under the name instead of
// under the whole declaration: without it the range is the node's.
void @CURSIV_NEW_CLASS@Validator::checkPersonStartsWithCapital(
    const ast::Person& person, cursiv::ValidationAcceptor& accept) const {
  if (!person.name.empty() && person.name.front() >= 'a' &&
      person.name.front() <= 'z')
    accept.warning(person, "Person name should start with a capital.")
        .property<&ast::Person::name>();
}

void @CURSIV_NEW_CLASS@Validator::checkUniquePersons(
    const ast::Model& model, cursiv::ValidationAcceptor& accept) const {
  std::unordered_map<std::string_view, std::vector<const ast::Person*>> names;
  for (const ast::Person* const person : model.persons)
    if (person != nullptr) names[person->name].push_back(person);

  for (const auto& [name, people] : names) {
    if (people.size() <= 1) continue;
    const std::string message = "Duplicate person name: " + std::string(name);
    for (const ast::Person* const person : people)
      accept.error(*person, message).property<&ast::Person::name>();
  }
}

// A check is registered against the AST class it judges: the validator walks
// the tree once and calls each check on the nodes of its own type.
void register_validation_checks(@CURSIV_NEW_CLASS@CoreServices& services) {
  using V = @CURSIV_NEW_CLASS@Validator;
  services.validator = std::make_unique<const V>(services);
  const V& validator = *services.validator;
  services.validationRegistry->registerChecks(
      {cursiv::ValidationRegistry::makeValidationCheck<
           &V::checkPersonStartsWithCapital>(validator),
       cursiv::ValidationRegistry::makeValidationCheck<&V::checkUniquePersons>(
           validator)});
}

}  // namespace @CURSIV_NEW_ID@
