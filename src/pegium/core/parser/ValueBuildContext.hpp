#pragma once

/// Inputs and callbacks used while materializing AST and rule values.

#include <string>
#include <vector>

#include <pegium/core/parser/ParseDiagnostic.hpp>
#include <pegium/core/syntax-tree/CstNodeView.hpp>
#include <pegium/core/syntax-tree/Reference.hpp>

namespace pegium {
class AstArena;
namespace references {
class Linker;
}
} // namespace pegium

namespace pegium::parser {

struct ValueBuildContext {
  std::vector<ReferenceHandle> *references = nullptr;
  const references::Linker *linker = nullptr;
  const grammar::Assignment *assignment = nullptr;
  std::vector<ParseDiagnostic> *diagnostics = nullptr;
  AstArena *arena = nullptr;

  [[nodiscard]] ValueBuildContext
  withAssignment(const grammar::Assignment &nextAssignment) const noexcept {
    auto copy = *this;
    copy.assignment = std::addressof(nextAssignment);
    return copy;
  }

  void addConversionDiagnostic(const CstNodeView &node,
                               const grammar::AbstractElement *element,
                               std::string message) const {
    if (diagnostics == nullptr) {
      return;
    }
    diagnostics->push_back(ParseDiagnostic{
        .kind = ParseDiagnosticKind::ConversionError,
        .offset = node.getBegin(),
        .beginOffset = node.getBegin(),
        .endOffset = node.getEnd(),
        .element = element,
        .message = std::move(message),
    });
  }
};

} // namespace pegium::parser
