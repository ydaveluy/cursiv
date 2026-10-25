#pragma once

#include "arithmetics/core/ast.hpp"
#include "cursiv/lsp/formatting/AbstractFormatter.hpp"

namespace arith {

class ArithmeticsFormatter : public cursiv::AbstractFormatter {
 public:
  explicit ArithmeticsFormatter(const cursiv::CoreServices& services);

 protected:
  virtual void formatModule(cursiv::FormattingBuilder& builder,
                            const ast::Module* module) const;
  virtual void formatDefinition(cursiv::FormattingBuilder& builder,
                                const ast::Definition* definition) const;
  virtual void formatEvaluation(cursiv::FormattingBuilder& builder,
                                const ast::Evaluation* evaluation) const;
  virtual void formatBinaryExpression(
      cursiv::FormattingBuilder& builder,
      const ast::BinaryExpression* binary) const;
  virtual void formatGroupedExpression(
      cursiv::FormattingBuilder& builder,
      const ast::GroupedExpression* grouped) const;
  virtual void formatFunctionCall(cursiv::FormattingBuilder& builder,
                                  const ast::FunctionCall* call) const;
  virtual void formatComment(HiddenNodeFormatter& comment) const;
};

}  // namespace arith
