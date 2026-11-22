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
#include "domainmodel/ast.hpp"

// A method's first parameter is the type it watches, so the list in
// `CoreModule.cpp` is the whole registration. The cycle check exists because
// an `extends` cycle hangs every reader of the hierarchy.

namespace dm {

class DomainModelValidator final : public cursiv::DefaultCoreService,
                                   private cursiv::DocumentUpdateListener {
 public:
  explicit DomainModelValidator(const cursiv::CoreServices& services);
  ~DomainModelValidator() noexcept override;

  DomainModelValidator(const DomainModelValidator&) = delete;
  DomainModelValidator& operator=(const DomainModelValidator&) = delete;

  void checkEntityNameStartsWithCapital(
      const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const;
  void checkDataTypeNameStartsWithCapital(
      const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const;
  void checkEntityDoesNotExtendItself(const ast::Entity& entity,
                                      cursiv::ValidationAcceptor& accept) const;

 private:
  // Where the walk from an entity leaves its document: `ends` if it ends
  // there, `next` if it goes on at an entity of another document, neither if
  // it loops there.
  struct Exit {
    const ast::Entity* next = nullptr;
    bool ends = false;
  };

  // The walk from `entity` within its document. It stores the answer of each
  // entity it passes, so a later walk stops at the first known one.
  [[nodiscard]] Exit exitOf(const ast::Entity& entity) const;

  // `exitOf`'s answers, emptied when the document is linked again. A change
  // further up the chain, in another document, goes through `onUpdate`.
  cursiv::DocumentCache<const ast::Entity*, Exit> _exits{
      shared, cursiv::DocumentState::Linked};

  // Sends a document back to validation when an update moves a document its
  // walks went through, such as the one of `B` when `A` extends `B`.
  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // An entity a walk started from or entered another document at.
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
  // Where the walk from a step last entered the next document, every step whose
  // walk has entered here, and whether the walk ends, once known in this build.
  struct Walked {
    Step next;
    std::vector<Step> from;
    std::optional<bool> ends;
  };

  // Notes that the walk from `from` enters another document at `to`, and says
  // whether the walk from `to` ends, if known.
  [[nodiscard]] std::optional<bool> noteStep(const ast::Entity& from,
                                             const ast::Entity& to) const;
  // Notes whether the walks from `start` and from each entity it entered end.
  void noteEnds(const ast::Entity& start,
                std::span<const ast::Entity* const> entered, bool ends) const;

  mutable std::mutex _stepsMutex;
  mutable std::unordered_map<Step, Walked, StepHash> _steps;
};

}  // namespace dm
