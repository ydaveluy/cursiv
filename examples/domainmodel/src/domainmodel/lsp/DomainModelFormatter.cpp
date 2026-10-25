#include "domainmodel/lsp/DomainModelFormatter.hpp"

namespace dm {

namespace {

[[nodiscard]] bool has_doc_marker(std::string_view text) noexcept {
  return text.starts_with("/**");
}

}  // namespace

void DomainModelFormatter::formatDomainModel(
    cursiv::FormattingBuilder& builder, const ast::DomainModel* model) const {
  auto formatter = builder.getNodeFormatter(model);
  formatter.properties<&ast::DomainModel::elements>().prepend(noIndent);
}

void DomainModelFormatter::formatPackageDeclaration(
    cursiv::FormattingBuilder& builder,
    const ast::PackageDeclaration* package) const {
  auto formatter = builder.getNodeFormatter(package);
  formatter.keyword("package").append(oneSpace);
  const auto openBrace = formatter.keyword("{");
  const auto closeBrace = formatter.keyword("}");
  formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
}

void DomainModelFormatter::formatEntity(cursiv::FormattingBuilder& builder,
                                        const ast::Entity* entity) const {
  auto formatter = builder.getNodeFormatter(entity);
  formatter.keyword("entity").append(oneSpace);
  // A reference written nowhere carries no text: that is how an entity with
  // no `extends` is spelled.
  if (!entity->superType.refText.empty()) {
    formatter.keyword("extends").prepend(oneSpace).append(oneSpace);
  }
  const auto openBrace = formatter.keyword("{");
  const auto closeBrace = formatter.keyword("}");
  formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
}

void DomainModelFormatter::formatDataType(cursiv::FormattingBuilder& builder,
                                          const ast::DataType* dataType) const {
  auto formatter = builder.getNodeFormatter(dataType);
  formatter.keyword("datatype").append(oneSpace);
}

void DomainModelFormatter::formatFeature(cursiv::FormattingBuilder& builder,
                                         const ast::Feature* feature) const {
  auto formatter = builder.getNodeFormatter(feature);
  if (feature->many) {
    formatter.keyword("many").append(oneSpace);
  }
  formatter.keyword(":").prepend(noSpace).append(oneSpace);
}

void DomainModelFormatter::formatMultilineComment(
    HiddenNodeFormatter& comment) const {
  const auto docComment = has_doc_marker(comment.text());
  comment.replace(cursiv::AbstractFormatter::formatMultilineComment(
      comment, {.start = docComment ? "/**" : "/*",
                       .end = "*/",
                       .newLineStart = docComment ? " *" : "",
                       .tagStart = docComment ? std::optional<std::string>{"@"}
                                              : std::nullopt}));
}

void DomainModelFormatter::formatLineComment(
    HiddenNodeFormatter& comment) const {
  comment.replace(cursiv::AbstractFormatter::formatLineComment(comment));
}

DomainModelFormatter::DomainModelFormatter(const cursiv::CoreServices& services)
    : AbstractFormatter(services) {
  on<ast::DomainModel>(&DomainModelFormatter::formatDomainModel);
  on<ast::PackageDeclaration>(&DomainModelFormatter::formatPackageDeclaration);
  on<ast::Entity>(&DomainModelFormatter::formatEntity);
  on<ast::DataType>(&DomainModelFormatter::formatDataType);
  on<ast::Feature>(&DomainModelFormatter::formatFeature);
  onHidden("ML_COMMENT", &DomainModelFormatter::formatMultilineComment);
  onHidden("SL_COMMENT", &DomainModelFormatter::formatLineComment);
}

}  // namespace dm
