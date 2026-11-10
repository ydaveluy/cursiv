#include "workspace/core/validation/WorkspaceValidator.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"

namespace ws {

namespace {

bool starts_lower(std::string_view name) {
  return !name.empty() && name.front() >= 'a' && name.front() <= 'z';
}

// The entities a walk passed, to tell when it comes back to one: searched in
// place while few, as most walks are, and hashed past that.
class Passed {
 public:
  void add(const ast::Entity* entity) {
    _list.push_back(entity);
    if (!_set.empty())
      _set.insert(entity);
    else if (_list.size() == kFew)
      _set.insert(_list.begin(), _list.end());
  }
  [[nodiscard]] bool contains(const ast::Entity* entity) const {
    return _set.empty() ? std::ranges::find(_list, entity) != _list.end()
                        : _set.contains(entity);
  }
  [[nodiscard]] const std::vector<const ast::Entity*>& all() const {
    return _list;
  }

 private:
  static constexpr std::size_t kFew = 32;
  std::vector<const ast::Entity*> _list;
  std::unordered_set<const ast::Entity*> _set;
};

}  // namespace

WorkspaceValidator::WorkspaceValidator(const cursiv::CoreServices& services)
    : cursiv::DefaultCoreService(services) {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->addDocumentUpdateListener(*this);
}

WorkspaceValidator::~WorkspaceValidator() noexcept {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->removeDocumentUpdateListener(*this);
}

void WorkspaceValidator::noteChainedDocument(cursiv::DocumentId from,
                                             cursiv::DocumentId through) const {
  const std::scoped_lock lock(_chainedMutex);
  _chainedDocuments[from].insert(through);
}

// Called after the update's resets. A validated document whose chains went
// through a removed or unlinked document goes back to validation; any other
// drops its entry, which its next validation writes again.
void WorkspaceValidator::onUpdate(std::span<const std::string_view>,
                                  std::span<const std::string_view>) {
  const cursiv::Documents& store = *shared.documents;
  const auto moved = [&store](cursiv::DocumentId id) {
    const std::shared_ptr<cursiv::Document> document = store.getDocument(id);
    return document == nullptr ||
           document->state.load() < cursiv::DocumentState::IndexedReferences;
  };
  const std::scoped_lock lock(_chainedMutex);
  for (auto at = _chainedDocuments.begin(); at != _chainedDocuments.end();) {
    const std::shared_ptr<cursiv::Document> document =
        store.getDocument(at->first);
    if (document != nullptr &&
        document->state.load() == cursiv::DocumentState::Validated) {
      if (std::ranges::none_of(at->second, moved)) {
        ++at;
        continue;
      }
      shared.documentBuilder->resetToState(
          *document, cursiv::DocumentState::IndexedReferences);
    }
    at = _chainedDocuments.erase(at);
  }
}

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

// Up the chain to its end, to another document, to a known entity, or back to
// an entity passed: a loop, as `A -> B -> C -> B` is without reaching `A`
// again. Every entity passed shares the answer.
WorkspaceValidator::Exit WorkspaceValidator::exitOf(
    const ast::Entity& entity) const {
  const cursiv::Document* const document = entity.getDocument();
  const cursiv::DocumentId id =
      document == nullptr ? cursiv::kNoDocument : document->id;
  return _exits.get(id, &entity, [&] {
    // The entities past `entity`: most walks pass none.
    Passed passed;
    Exit exit;
    for (const ast::Entity* at = &entity;;) {
      const ast::Entity* const super = at->superType.get();
      if (super == nullptr) {
        exit = {.ends = true};
        break;
      }
      if (super->getDocument() != document) {
        exit = {.next = super};
        break;
      }
      if (super == &entity || passed.contains(super)) break;
      if (const std::optional<Exit> known = _exits.get(id, super)) {
        exit = *known;
        break;
      }
      passed.add(super);
      at = super;
    }
    for (const ast::Entity* at : passed.all())
      (void)_exits.get(id, at, [&] { return exit; });
    return exit;
  });
}

// Follows the chain document by document: entering an entity twice is a loop.
// Each document entered is noted, since the verdict depends on it.
void WorkspaceValidator::checkEntityDoesNotExtendItself(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  const cursiv::Document* const entityDocument = entity.getDocument();
  Passed entered;
  for (const ast::Entity* at = &entity;;) {
    const Exit exit = exitOf(*at);
    if (exit.ends) return;
    if (exit.next == nullptr || exit.next == &entity ||
        entered.contains(exit.next))
      break;
    if (const cursiv::Document* const next = exit.next->getDocument();
        entityDocument != nullptr && next != nullptr && next != entityDocument)
      noteChainedDocument(entityDocument->id, next->id);
    entered.add(exit.next);
    at = exit.next;
  }
  accept.error(entity, "Entity extends itself, directly or through a cycle.")
      .property<&ast::Entity::superType>();
}

}  // namespace ws
