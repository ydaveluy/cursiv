#include "cursiv/test/AstText.hpp"

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"

namespace cursiv::test {

std::string records(const Cst& cst) {
  std::string out;
  for (NodeId i = 0; i < cst.size(); ++i) {
    const CstNode& n = cst[i];
    out += std::to_string(n.begin) + ',' + std::to_string(n.end) + ',' +
           std::to_string(n.subtreeSize) + ',' + (n.hidden ? 'h' : '.') +
           (n.recovered ? 'r' : '.') + ';';
  }
  return out;
}

void ast_of(const AstReflection& r, const AstNode& n, std::string& out) {
  const auto meta = r.getTypeMetaData(n.type);
  out += meta.name;
  out += '{';
  r.visitMembers(n,
                 [&](std::string_view p, std::uint32_t i, const AstNode& kid) {
                   out += p;
                   if (i != AstReflection::kNoIndex) out += std::to_string(i);
                   out += '=';
                   ast_of(r, kid, out);
                   out += ' ';
                 });
  out += '}';
}

void spans_of(const AstReflection& r, const Cst& cst, const AstNode& n,
              std::string& out) {
  const CstNode& at = cst[n.cstNodeId];
  out += r.getTypeMetaData(n.type).name;
  out += '[' + std::to_string(at.begin) + ',' + std::to_string(at.end) + ')';
  r.visitMembers(n, [&](std::string_view, std::uint32_t, const AstNode& kid) {
    out += ' ';
    spans_of(r, cst, kid, out);
  });
}

}  // namespace cursiv::test
