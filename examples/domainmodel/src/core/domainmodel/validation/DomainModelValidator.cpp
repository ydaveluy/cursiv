#include "domainmodel/validation/DomainModelValidator.hpp"

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

namespace dm {

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

DomainModelValidator::DomainModelValidator(const cursiv::CoreServices& services)
    : cursiv::DefaultCoreService(services) {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->addDocumentUpdateListener(*this);
}

DomainModelValidator::~DomainModelValidator() noexcept {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->removeDocumentUpdateListener(*this);
}

std::optional<bool> DomainModelValidator::noteStep(
    const ast::Entity& from, const ast::Entity& to) const {
  const cursiv::Document* const left = from.getDocument();
  const cursiv::Document* const entered = to.getDocument();
  if (left == nullptr || entered == nullptr) return std::nullopt;
  const Step source{.document = left->id, .entity = &from};
  const Step target{.document = entered->id, .entity = &to};
  const std::scoped_lock lock(_stepsMutex);
  Walked& onward = _steps[target];
  if (Walked& here = _steps[source]; here.next != target) {
    onward.from.push_back(source);
    here.next = target;
  }
  return onward.ends;
}

void DomainModelValidator::noteEnds(const ast::Entity& start,
                                    std::span<const ast::Entity* const> entered,
                                    bool ends) const {
  const std::scoped_lock lock(_stepsMutex);
  const auto mark = [this, ends](const ast::Entity& entity) {
    if (const cursiv::Document* const document = entity.getDocument())
      if (const auto found = _steps.find({document->id, &entity});
          found != _steps.end())
        found->second.ends = ends;
  };
  mark(start);
  for (const ast::Entity* const entity : entered) mark(*entity);
}

// Forgets every answer, and drops the steps of documents now gone or unlinked
// with every step whose walk went on through one of them. A validated document
// that loses a step goes back to validation.
void DomainModelValidator::onUpdate(std::span<const std::string_view>,
                                    std::span<const std::string_view>) {
  const cursiv::Documents& store = *shared.documents;
  const std::scoped_lock lock(_stepsMutex);
  std::vector<Step> pending;
  for (auto& [step, walked] : _steps) {
    walked.ends.reset();
    if (const std::shared_ptr<cursiv::Document> document =
            store.getDocument(step.document);
        document == nullptr
        || document->state.load() < cursiv::DocumentState::IndexedReferences)
      pending.push_back(step);
  }
  if (pending.empty()) return;
  std::unordered_set<Step, StepHash> reached(pending.begin(), pending.end());
  while (!pending.empty()) {
    const auto found = _steps.find(pending.back());
    pending.pop_back();
    if (found == _steps.end()) continue;
    for (const Step& from : found->second.from)
      if (reached.insert(from).second) pending.push_back(from);
  }
  std::unordered_set<cursiv::DocumentId> sent;
  for (const Step& step : reached) {
    if (!sent.insert(step.document).second) continue;
    const std::shared_ptr<cursiv::Document> document =
        store.getDocument(step.document);
    if (document != nullptr
        && document->state.load() == cursiv::DocumentState::Validated)
      shared.documentBuilder->resetToState(
          *document, cursiv::DocumentState::IndexedReferences);
  }
  for (auto at = _steps.begin(); at != _steps.end();) {
    if (reached.contains(at->first)) {
      at = _steps.erase(at);
      continue;
    }
    std::erase_if(at->second.from, [&reached](const Step& from) {
      return reached.contains(from);
    });
    ++at;
  }
}

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

// Up the chain to its end, to another document, to an entity already
// answered, or back to an entity it passed: a loop, which `A -> B -> C -> B`
// is without coming back to `A`. Every entity passed shares the answer.
DomainModelValidator::Exit DomainModelValidator::exitOf(
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

// From one document to the next: a walk that enters an entity twice loops.
// Each step into another document is noted, and the answer is noted for every
// entity entered, so a later walk stops at the first one answered.
void DomainModelValidator::checkEntityDoesNotExtendItself(
    const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const {
  Passed entered;
  bool ends = false;
  for (const ast::Entity* at = &entity;;) {
    const Exit exit = exitOf(*at);
    if (exit.ends) {
      ends = true;
      break;
    }
    if (exit.next == nullptr) break;
    const std::optional<bool> known = noteStep(*at, *exit.next);
    if (exit.next == &entity || entered.contains(exit.next)) break;
    entered.add(exit.next);
    if (known) {
      ends = *known;
      break;
    }
    at = exit.next;
  }
  if (!entered.all().empty()) noteEnds(entity, entered.all(), ends);
  if (!ends)
    accept.error(entity, "Entity extends itself, directly or through a cycle.")
        .property<&ast::Entity::superType>();
}

}  // namespace dm
