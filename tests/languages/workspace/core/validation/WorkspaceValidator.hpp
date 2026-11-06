#pragma once

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/DocumentCache.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "workspace/core/ast.hpp"

// A method's first parameter is the type it watches, so the list in
// `CoreModule.cpp` is the whole registration. The cycle check exists because
// an `extends` cycle hangs every reader of the hierarchy.

namespace ws {

class WorkspaceValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkEntityNameStartsWithCapital(
      const ast::Entity& entity, cursiv::ValidationAcceptor& accept) const;
  void checkDataTypeNameStartsWithCapital(
      const ast::DataType& dataType, cursiv::ValidationAcceptor& accept) const;
  void checkEntityDoesNotExtendItself(const ast::Entity& entity,
                                      cursiv::ValidationAcceptor& accept) const;

 private:
  // Where the climb from an entity leaves its document: `ends` if it ends
  // there, `next` if it goes on at an entity of another document, neither if
  // it loops there.
  struct Exit {
    const ast::Entity* next = nullptr;
    bool ends = false;
  };

  // The climb from `entity` within its document. It stores the answer of
  // every entity it passes, so a later climb stops at the first one already
  // answered: a document costs one pass whatever order its chains are
  // written in, and a climb holds no frame per storey.
  [[nodiscard]] Exit exitOf(const ast::Entity& entity) const;

  // `exitOf`, memoised -- unmemoised the check is quadratic over a document.
  // An answer reads its document and the entities its references resolve to,
  // nothing further: a change to those relinks the document, and its entries
  // go when it is linked again. A change further up the chain relinks
  // nothing here, so the check walks from one document to the next instead
  // of keeping a whole climb.
  cursiv::DocumentCache<const ast::Entity*, Exit> _exits{
      shared, cursiv::DocumentState::Linked};
};

}  // namespace ws
