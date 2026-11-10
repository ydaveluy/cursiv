#include "arithmetics/Evaluator.hpp"

#include <ostream>

#include "arithmetics/Interpreter.hpp"
#include "cursiv/cli.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace arith {

int eval_file(std::string_view path, cursiv::SharedCoreServices& shared,
              std::ostream& out, std::ostream& err) {
  const cursiv::Document& document =
      cursiv::build_document_from_path(path, shared);
  if (cursiv::has_error_diagnostics(document)) {
    err << "There are validation errors:\n";
    cursiv::print_diagnostics(document, path, err);
    return cursiv::kDiagnostics;
  }

  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::Module>) {
    err << "Unable to evaluate invalid arithmetics module.\n";
    return cursiv::kDiagnostics;
  }

  const auto& module = static_cast<const ast::Module&>(*root);
  const EvaluationResult results = interpret_evaluations(module);
  const cursiv::TextDocument& text = document.textDocument();
  bool failed = false;
  for (const ast::Statement* const statement : module.statements) {
    if (statement == nullptr
        || statement->type != &cursiv::ast_type_tag<ast::Evaluation>)
      continue;
    const auto& evaluation = static_cast<const ast::Evaluation&>(*statement);
    const auto found = results.find(&evaluation);
    if (found == results.end() || evaluation.expression == nullptr) continue;

    const cursiv::CstNode& span =
        document.parseResult.cst()[evaluation.expression->cstNodeId];
    const auto line = text.positionAt(span.begin).line + 1;
    const std::string_view source =
        text.getText().substr(span.begin, span.end - span.begin);
    // A valid module can still divide by a zero that a parameter brings: the
    // evaluation is reported, not dropped.
    if (!found->second.has_value()) {
      failed = true;
      err << "line " << line << ": " << source << " cannot be evaluated\n";
      continue;
    }
    out << "line " << line << ": " << source << " ===> "
        << number_text(*found->second) << '\n';
  }
  return failed ? cursiv::kDiagnostics : cursiv::kClean;
}

}  // namespace arith
