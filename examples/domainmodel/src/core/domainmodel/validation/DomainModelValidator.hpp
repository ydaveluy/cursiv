#pragma once

#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

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
  void noteDependency(cursiv::DocumentId from,
                      cursiv::DocumentId through) const;

  mutable std::mutex _dependenciesMutex;
  mutable std::unordered_map<cursiv::DocumentId,
                             std::unordered_set<cursiv::DocumentId>>
      _dependencies;
};

}  // namespace dm
