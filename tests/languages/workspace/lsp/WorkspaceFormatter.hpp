#pragma once

#include "cursiv/lsp/formatting/AbstractFormatter.hpp"
#include "workspace/core/ast.hpp"

namespace ws {

class WorkspaceFormatter : public cursiv::AbstractFormatter {
 public:
  explicit WorkspaceFormatter(const cursiv::CoreServices& services);

 protected:
  virtual void formatWorkspace(cursiv::FormattingBuilder& builder,
                                 const ast::Workspace* model) const;
  virtual void formatPackageDeclaration(
      cursiv::FormattingBuilder& builder,
      const ast::PackageDeclaration* package) const;
  virtual void formatEntity(cursiv::FormattingBuilder& builder,
                            const ast::Entity* entity) const;
  virtual void formatDataType(cursiv::FormattingBuilder& builder,
                              const ast::DataType* dataType) const;
  virtual void formatFeature(cursiv::FormattingBuilder& builder,
                             const ast::Feature* feature) const;
  virtual void formatMultilineComment(HiddenNodeFormatter& comment) const;
  virtual void formatLineComment(HiddenNodeFormatter& comment) const;
};

}  // namespace ws
