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
  void noteChainedDocument(cursiv::DocumentId from,
                           cursiv::DocumentId through) const;

  mutable std::mutex _chainedMutex;
  mutable std::unordered_map<cursiv::DocumentId,
                             std::unordered_set<cursiv::DocumentId>>
      _chainedDocuments;
};

}  // namespace ws
