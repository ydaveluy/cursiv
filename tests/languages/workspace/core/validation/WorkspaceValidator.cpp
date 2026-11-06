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

void WorkspaceValidator::noteClimb(cursiv::DocumentId from,
                                   cursiv::DocumentId through) const {
  const std::scoped_lock lock(_climbedMutex);
  _climbed[from].insert(through);
}

// Announced once this update's resets are done: a document the climbs went
// through moved if it is gone or no longer linked. A validated document whose
// climbs went through one goes back to validation; any other drops its entry,
// which its next validation writes again.
void WorkspaceValidator::onUpdate(std::span<const std::string_view>,
                                  std::span<const std::string_view>) {
  const cursiv::Documents& store = *shared.documents;
  const auto moved = [&store](cursiv::DocumentId id) {
    const std::shared_ptr<cursiv::Document> document = store.getDocument(id);
    return document == nullptr ||
           document->state.load() < cursiv::DocumentState::IndexedReferences;
  };
  const std::scoped_lock lock(_climbedMutex);
  for (auto at = _climbed.begin(); at != _climbed.end();) {
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
    at = _climbed.erase(at);
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

// Up the chain to its end, to another document, to an entity already
// answered, or back to an entity it passed: a loop, which `A -> B -> C -> B`
// is without coming back to `A`. Every entity passed shares the answer.
WorkspaceValidator::Exit WorkspaceValidator::exitOf(
    const ast::Entity& entity) const {
  const cursiv::Document* const document = entity.getDocument();
  const cursiv::DocumentId id =
      document == nullptr ? cursiv::kNoDocument : document->id;
  return _exits.get(id, &entity, [&] {
    // The entities past `entity`: most climbs pass none.
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

// From one document to the next: a climb that enters an entity twice loops.
// Each document it enters is noted, since the verdict reads it.
void WorkspaceValidator::checkEntityDoesNotExtendItself(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  const cursiv::Document* const home = entity.getDocument();
  Passed entered;
  for (const ast::Entity* at = &entity;;) {
    const Exit exit = exitOf(*at);
    if (exit.ends) return;
    if (exit.next == nullptr || exit.next == &entity ||
        entered.contains(exit.next))
      break;
    if (const cursiv::Document* const next = exit.next->getDocument();
        home != nullptr && next != nullptr && next != home)
      noteClimb(home->id, next->id);
    entered.add(exit.next);
    at = exit.next;
  }
  accept.error(entity, "Entity extends itself, directly or through a cycle.")
      .property<&ast::Entity::superType>();
}

}  // namespace ws
