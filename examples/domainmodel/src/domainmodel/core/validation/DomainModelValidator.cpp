#include "domainmodel/core/validation/DomainModelValidator.hpp"

#include <string_view>
#include <vector>

#include "cursiv/core/workspace/Document.hpp"

namespace dm {

namespace {

bool starts_lower(std::string_view name) {
  return !name.empty() && name.front() >= 'a' && name.front() <= 'z';
}

}  // namespace

void DomainModelValidator::checkEntityNameStartsWithCapital(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  if (starts_lower(entity.name))
    accept.warning(entity, "Type name should start with a capital.")
        .property<&ast::AbstractElement::name>();
}

void DomainModelValidator::checkDataTypeNameStartsWithCapital(
    const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const {
  if (starts_lower(dataType.name))
    accept.warning(dataType, "Type name should start with a capital.")
        .property<&ast::AbstractElement::name>();
}

// Whether the climb from `entity` ends; memoized so it stays linear over a
// document -- unmemoized, this check is quadratic.
// `seen` is the whole climb, not a single node: `super == &entity` alone
// never terminates on a cycle `entity` is not part of (`A -> B -> C -> B`).
bool DomainModelValidator::terminates(
    const ast::Entity& entity, std::vector<const ast::Entity*>& seen) const {
  const cursiv::Document* const document = entity.getDocument();
  const cursiv::DocumentId id =
      document == nullptr ? cursiv::kNoDocument : document->id;
  return _terminates.get(id, &entity, [&] {
    const auto* const super = entity.superType.get();
    if (super == nullptr) return true;
    for (const ast::Entity* const on : seen)
      if (on == super) return false;
    seen.push_back(&entity);
    return terminates(*super, seen);
  });
}

void DomainModelValidator::checkEntityDoesNotExtendItself(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  std::vector<const ast::Entity*> seen;
  if (terminates(entity, seen)) return;
  accept.error(entity, "Entity extends itself, directly or through a cycle.")
      .property<&ast::Entity::superType>();
}

}  // namespace dm
