// A parse written as text to compare: CST records by span, size and flags; the
// AST by class and member, or by span. No node id: each engine numbers nodes
// its own way.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/Cst.hpp"

namespace support {

inline std::string records(const cursiv::Cst& cst) {
  std::string out;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    out += std::to_string(n.begin) + ',' + std::to_string(n.end) + ',' +
           std::to_string(n.subtreeSize) + ',' + (n.hidden ? 'h' : '.') +
           (n.recovered ? 'r' : '.') + ';';
  }
  return out;
}

inline void ast_of(const cursiv::AstReflection& r, const cursiv::AstNode& n,
                   std::string& out) {
  const auto meta = r.getTypeMetaData(n.type);
  out += meta.name;
  out += '{';
  r.visitProperties(n, [&](std::string_view p, std::uint32_t i,
                           const cursiv::AstNode& child) {
    out += p;
    if (i != cursiv::AstReflection::kNoIndex) out += std::to_string(i);
    out += '=';
    ast_of(r, child, out);
    out += ' ';
  });
  out += '}';
}

inline void spans_of(const cursiv::AstReflection& r, const cursiv::Cst& cst,
                     const cursiv::AstNode& n, std::string& out) {
  const cursiv::CstNode& at = cst[n.cstNodeId];
  out += r.getTypeMetaData(n.type).name;
  out += '[' + std::to_string(at.begin) + ',' + std::to_string(at.end) + ')';
  r.visitProperties(
      n, [&](std::string_view, std::uint32_t, const cursiv::AstNode& child) {
        out += ' ';
        spans_of(r, cst, child, out);
      });
}

}  // namespace support
