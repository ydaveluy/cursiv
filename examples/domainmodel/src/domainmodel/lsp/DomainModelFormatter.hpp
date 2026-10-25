#pragma once

#include "cursiv/lsp/formatting/AbstractFormatter.hpp"
#include "domainmodel/core/ast.hpp"

namespace dm {

class DomainModelFormatter : public cursiv::AbstractFormatter {
 public:
  explicit DomainModelFormatter(const cursiv::CoreServices& services);

 protected:
  virtual void formatDomainModel(cursiv::FormattingBuilder& builder,
                                 const ast::DomainModel* model) const;
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

}  // namespace dm
