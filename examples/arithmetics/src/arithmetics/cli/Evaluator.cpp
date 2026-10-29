#include "arithmetics/cli/Evaluator.hpp"

#include <iostream>

#include "arithmetics/core/Interpreter.hpp"
#include "cursiv/cli/cli.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"

namespace arith {

int eval_file(std::string_view path, cursiv::SharedCoreServices& shared) {
  const cursiv::Document& document =
      cursiv::build_document_from_path(path, shared);
  if (cursiv::has_error_diagnostics(document)) {
    std::cerr << "There are validation errors:\n";
    cursiv::print_diagnostics(document, path, std::cerr);
    return cursiv::kDiagnostics;
  }

  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::Module>) {
    std::cerr << "Unable to evaluate invalid arithmetics module.\n";
    return cursiv::kDiagnostics;
  }

  const auto& module = static_cast<const ast::Module&>(*root);
  const EvaluationResult results = interpret_evaluations(module);
  const cursiv::TextDocument& text = document.textDocument();
  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr
        || statement->type != &cursiv::ast_type_tag<ast::Evaluation>)
      continue;
    const auto& evaluation = static_cast<const ast::Evaluation&>(*statement);
    const auto found = results.find(&evaluation);
    if (found == results.end() || evaluation.expression == nullptr) continue;

    const cursiv::CstNode& span =
        document.parseResult.cst()[evaluation.expression->cstNodeId];
    std::cout << "line " << text.positionAt(span.begin).line + 1 << ": "
              << text.getText().substr(span.begin, span.end - span.begin)
              << " ===> " << found->second << '\n';
  }
  return cursiv::kClean;
}

}  // namespace arith
