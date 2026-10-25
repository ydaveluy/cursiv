#pragma once

#include <string_view>

#include "arithmetics/core/ast.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"

// The six checks of this language. The last one folds constants and carries the
// folded value as diagnostic data, which is what the code action reads to offer
// the rewrite.

namespace arith {

// The code a quick fix looks for. A string, because that is what crosses the
// wire in a diagnostic.
inline constexpr std::string_view kExpressionNormalizable =
    "expression-normalizable";

class ArithmeticsValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  // Document-level, not per node: one bounded walk prices every divisor,
  // and the whole check stays O(divisions), never the call graph.
  void checkDivisionsByZero(const cursiv::Document& document,
                            cursiv::ValidationAcceptor& accept,
                            const cursiv::CancellationToken& token) const;
  void checkUniqueParameters(const ast::Definition& definition,
                             cursiv::ValidationAcceptor& accept) const;
  void checkUniqueDefinitions(const ast::Module& module,
                              cursiv::ValidationAcceptor& accept) const;
  void checkMatchingParameters(const ast::FunctionCall& call,
                               cursiv::ValidationAcceptor& accept) const;
  void checkFunctionRecursion(const ast::Module& module,
                              cursiv::ValidationAcceptor& accept) const;
  void checkNormalizable(const ast::Definition& definition,
                         cursiv::ValidationAcceptor& accept) const;
};

}  // namespace arith
