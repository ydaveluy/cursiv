#include "workspace/core/validation/WorkspaceValidator.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

#include "cursiv/workspace/Document.hpp"

namespace ws {

namespace {

bool starts_lower(std::string_view name) {
  return !name.empty() && name.front() >= 'a' && name.front() <= 'z';
}

}  // namespace

void WorkspaceValidator::checkEntityNameStartsWithCapital(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  if (starts_lower(entity.name))
    accept.warning(entity, "Type name should start with a capital.")
        .property<&ast::AbstractElement::name>();
}

void WorkspaceValidator::checkDataTypeNameStartsWithCapital(
    const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const {
  if (starts_lower(dataType.name))
    accept.warning(dataType, "Type name should start with a capital.")
        .property<&ast::AbstractElement::name>();
}

// `seen` is the whole climb, not a single node: `super == &entity` alone
// never stops on a loop `entity` is not part of (`A -> B -> C -> B`).
WorkspaceValidator::Exit WorkspaceValidator::exitOf(
    const ast::Entity& entity, std::vector<const ast::Entity*>& seen) const {
  const cursiv::Document* const document = entity.getDocument();
  const cursiv::DocumentId id =
      document == nullptr ? cursiv::kNoDocument : document->id;
  return _exits.get(id, &entity, [&]() -> Exit {
    const auto* const super = entity.superType.get();
    if (super == nullptr) return {.ends = true};
    if (super->getDocument() != document) return {.next = super};
    if (std::ranges::find(seen, super) != seen.end()) return {};
    seen.push_back(&entity);
    return exitOf(*super, seen);
  });
}

// From one document to the next: a climb that enters an entity twice loops.
void WorkspaceValidator::checkEntityDoesNotExtendItself(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  std::vector<const ast::Entity*> entered{&entity};
  std::vector<const ast::Entity*> seen;
  for (const ast::Entity* at = &entity;;) {
    seen.clear();
    const Exit exit = exitOf(*at, seen);
    if (exit.ends) return;
    if (exit.next == nullptr ||
        std::ranges::find(entered, exit.next) != entered.end())
      break;
    entered.push_back(exit.next);
    at = exit.next;
  }
  accept.error(entity, "Entity extends itself, directly or through a cycle.")
      .property<&ast::Entity::superType>();
}

}  // namespace ws
