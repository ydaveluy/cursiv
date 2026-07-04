#include <@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.hpp>

#include <cctype>

namespace @PEGIUM_NEW_LANGUAGE_ID@::validation {

void @PEGIUM_NEW_CLASS@Validator::checkPersonNameStartsWithCapital(
    const ast::Person &person,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (person.name.empty()) {
    return;
  }
  const auto first = person.name.front();
  if (std::toupper(static_cast<unsigned char>(first)) == first) {
    return;
  }
  accept.warning(person, "Person name should start with a capital.")
      .property<&ast::Person::name>();
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::validation
