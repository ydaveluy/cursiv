#pragma once

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "statemachine/ast.hpp"

// The three checks of this language: a naming convention, the one fact no
// single node can see (two declarations with one name), and the keyword a
// name may not be, which the grammar accepts and the validator refuses.

namespace sm {

class StateMachineValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkStateNameStartsWithCapital(
      const ast::State& state, cursiv::ValidationAcceptor& accept) const;
  void checkUniqueStatesAndEvents(const ast::Statemachine& model,
                                  cursiv::ValidationAcceptor& accept) const;
  void checkNamesAreNotKeywords(const ast::Statemachine& model,
                                cursiv::ValidationAcceptor& accept) const;
};

}  // namespace sm
