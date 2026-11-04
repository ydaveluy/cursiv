// A parse's two answers, spelled so two of them compare: the records by span,
// extent and standing, the AST by class and member or by class and span. An
// element id is each engine's own numbering and says nothing across two runs,
// so no spelling carries one.
#pragma once

#include <string>

namespace cursiv {
struct AstNode;
struct AstReflection;
class Cst;
}

namespace cursiv::test {

std::string records(const Cst& cst);
void ast_of(const AstReflection& r, const AstNode& n, std::string& out);
void spans_of(const AstReflection& r, const Cst& cst, const AstNode& n,
              std::string& out);

}  // namespace cursiv::test
