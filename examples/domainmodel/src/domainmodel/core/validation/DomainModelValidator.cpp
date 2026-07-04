#include <domainmodel/core/validation/DomainModelValidator.hpp>

#include <domainmodel/core/ast.hpp>

#include <cctype>

namespace domainmodel::validation {
using namespace domainmodel::ast;

void DomainModelValidator::checkEntityNameStartsWithCapital(
    const Entity &entity,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (entity.name.empty()) {
    return;
  }
  const auto first = entity.name.front();
  if (std::toupper(static_cast<unsigned char>(first)) == first) {
    return;
  }
  accept.warning(entity, "Type name should start with a capital.")
      .property<&Entity::name>();
}

void DomainModelValidator::checkDataTypeNameStartsWithCapital(
    const DataType &dataType,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (dataType.name.empty()) {
    return;
  }
  const auto first = dataType.name.front();
  if (std::toupper(static_cast<unsigned char>(first)) == first) {
    return;
  }
  accept.warning(dataType, "Type name should start with a capital.")
      .property<&DataType::name>();
}

void DomainModelValidator::checkEntityDoesNotExtendItself(
    const Entity &entity,
    const pegium::validation::ValidationAcceptor &accept) const {
  if (entity.superType.has_value() && entity.superType->get() == &entity) {
    accept.error(entity, "An entity may not extend itself.")
        .keyword("extends");
  }
}

} // namespace domainmodel::validation
