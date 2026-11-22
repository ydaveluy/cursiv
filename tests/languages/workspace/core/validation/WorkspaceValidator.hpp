#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/DocumentCache.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "workspace/core/ast.hpp"

// Each check's first parameter is the node type it runs on, so the list in
// `CoreModule.cpp` is the whole registration.

namespace ws {

class WorkspaceValidator final : public cursiv::DefaultCoreService,
                                 private cursiv::DocumentUpdateListener {
 public:
  explicit WorkspaceValidator(const cursiv::CoreServices& services);
  ~WorkspaceValidator() noexcept override;

  WorkspaceValidator(const WorkspaceValidator&) = delete;
  WorkspaceValidator& operator=(const WorkspaceValidator&) = delete;

  void checkEntityNameStartsWithCapital(
      const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const;
  void checkDataTypeNameStartsWithCapital(
      const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const;
  void checkEntityDoesNotExtendItself(const ast::Entity& entity,
                                      cursiv::ValidationAcceptor& accept) const;

 private:
  // Where an entity's `extends` chain leaves its document: `ends` if it ends
  // there, `next` if it goes on in another document, neither if it loops.
  struct Exit {
    const ast::Entity* next = nullptr;
    bool ends = false;
  };

  // Follows the chain of `entity` within its document and stores the answer
  // of every entity it passes, so a later walk stops at the first known one:
  // a document costs one pass, whatever the order of its chains.
  [[nodiscard]] Exit exitOf(const ast::Entity& entity) const;

  // `exitOf` answers, dropped when their document is linked again. An answer
  // stops at the document's edge: a change in another document does not
  // relink this one.
  cursiv::DocumentCache<const ast::Entity*, Exit> _exits{
      shared, cursiv::DocumentState::Linked};

  // A change in a document a chain went through can change a verdict here
  // without relinking this document (`A` extends `B` of another document,
  // whose parent changed), so it sends this document back to validation.
  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // An entity a chain started from or entered another document at.
  struct Step {
    cursiv::DocumentId document = cursiv::kNoDocument;
    const ast::Entity* entity = nullptr;
    bool operator==(const Step&) const = default;
  };
  struct StepHash {
    [[nodiscard]] std::size_t operator()(const Step& step) const noexcept {
      return std::hash<const ast::Entity*>{}(step.entity);
    }
  };
  // Where the chain from a step last entered the next document, every step
  // whose chain has entered here, and whether the chain ends, once known in
  // this build.
  struct Chained {
    Step next;
    std::vector<Step> from;
    std::optional<bool> ends;
  };

  // Notes that the chain from `from` enters another document at `to`, and
  // returns the verdict of the chain from `to`, if known.
  [[nodiscard]] std::optional<bool> noteStep(const ast::Entity& from,
                                             const ast::Entity& to) const;
  // Notes the verdict of the chains from `start` and each entity it entered.
  void noteEnds(const ast::Entity& start,
                std::span<const ast::Entity* const> entered, bool ends) const;

  mutable std::mutex _stepsMutex;
  mutable std::unordered_map<Step, Chained, StepHash> _steps;
};

}  // namespace ws
