#include "requirements/validation/RequirementsValidator.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"

namespace req {

namespace {

bool has_digit(std::string_view value) {
  return std::ranges::any_of(
      value, [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::string requirement_message(std::string_view prefix, std::string_view name,
                                std::string_view suffix) {
  std::string message;
  message.reserve(prefix.size() + name.size() + suffix.size());
  message.append(prefix);
  message.append(name);
  message.append(suffix);
  return message;
}

}  // namespace

RequirementsValidator::RequirementsValidator(
    const cursiv::CoreServices& services)
    : cursiv::DefaultCoreService(services), _covered(shared) {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->addDocumentUpdateListener(*this);
}

RequirementsValidator::~RequirementsValidator() noexcept {
  if (shared.documentBuilder != nullptr)
    shared.documentBuilder->removeDocumentUpdateListener(*this);
}

void RequirementsValidator::onUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) {
  if (changed.empty() && deleted.empty()) return;
  for (const std::shared_ptr<cursiv::Document>& document :
       shared.documents->all())
    if (document->language == &services
        && document->state.load() == cursiv::DocumentState::Validated)
      shared.documentBuilder->resetToState(
          *document, cursiv::DocumentState::IndexedReferences);
}

void RequirementsValidator::checkRequirementNameContainsANumber(
    const ast::Requirement& requirement,
    cursiv::ValidationAcceptor& accept) const {
  if (!has_digit(requirement.name))
    accept
        .warning(requirement,
                 requirement_message("Requirement name ", requirement.name,
                                     " should contain a number."))
        .property<&ast::Requirement::name>();
}

std::shared_ptr<const RequirementsValidator::Covered>
RequirementsValidator::computeCoverage() const {
  const auto& documents = *shared.documents;
  auto referenced = std::make_shared<Covered>();
  for (const std::shared_ptr<cursiv::Document>& document : documents.all()) {
    const cursiv::AstNode* const root = document->parseResult.value;
    if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::TestModel>)
      continue;
    for (const ast::Test* const test :
         static_cast<const ast::TestModel*>(root)->tests) {
      if (test == nullptr) continue;
      for (const auto& reference : test->requirements)
        if (const ast::Requirement* const target = reference.get())
          referenced->insert(target);
    }
  }
  return referenced;
}

void RequirementsValidator::checkRequirementIsCoveredByATest(
    const ast::Requirement& requirement,
    cursiv::ValidationAcceptor& accept) const {
  bool uncovered = false;
  _covered.withValue(
      [this] {
        // Concurrent misses share the scan. The nested get rechecks after
        // taking the mutex; neither cache provider holds the cache's lock.
        const std::lock_guard lock(_coverageMutex);
        return _covered.get([this] { return computeCoverage(); });
      },
      [&](const std::shared_ptr<const Covered>& covered) {
        uncovered = covered != nullptr && !covered->contains(&requirement);
      });
  if (uncovered)
    accept.warning(requirement,
                   requirement_message("Requirement ", requirement.name,
                                       " not covered by a test."));
}

}  // namespace req
