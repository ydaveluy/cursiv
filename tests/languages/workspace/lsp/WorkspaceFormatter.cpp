#include "workspace/lsp/WorkspaceFormatter.hpp"

namespace ws {

namespace {

[[nodiscard]] bool has_doc_marker(std::string_view text) noexcept {
  return text.starts_with("/**");
}

}  // namespace

void WorkspaceFormatter::formatWorkspace(
    cursiv::FormattingBuilder& builder, const ast::Workspace* model) const {
  auto formatter = builder.getNodeFormatter(model);
  formatter.properties<&ast::Workspace::elements>().prepend(noIndent);
}

void WorkspaceFormatter::formatPackageDeclaration(
    cursiv::FormattingBuilder& builder,
    const ast::PackageDeclaration* package) const {
  auto formatter = builder.getNodeFormatter(package);
  formatter.keyword("package").append(oneSpace);
  const auto openBrace = formatter.keyword("{");
  const auto closeBrace = formatter.keyword("}");
  formatBlock(openBrace, closeBrace, formatter.interior(openBrace, closeBrace));
}

void WorkspaceFormatter::formatEntity(cursiv::FormattingBuilder& builder,
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

void WorkspaceFormatter::formatDataType(cursiv::FormattingBuilder& builder,
                                          const ast::DataType* dataType) const {
  auto formatter = builder.getNodeFormatter(dataType);
  formatter.keyword("datatype").append(oneSpace);
}

void WorkspaceFormatter::formatFeature(cursiv::FormattingBuilder& builder,
                                         const ast::Feature* feature) const {
  auto formatter = builder.getNodeFormatter(feature);
  if (feature->many) {
    formatter.keyword("many").append(oneSpace);
  }
  formatter.keyword(":").prepend(noSpace).append(oneSpace);
}

void WorkspaceFormatter::formatMultilineComment(
    HiddenNodeFormatter& comment) const {
  const auto docComment = has_doc_marker(comment.text());
  comment.replace(cursiv::AbstractFormatter::formatMultilineComment(
      comment, {.start = docComment ? "/**" : "/*",
                       .end = "*/",
                       .newLineStart = docComment ? " *" : "",
                       .tagStart = docComment ? std::optional<std::string>{"@"}
                                              : std::nullopt}));
}

void WorkspaceFormatter::formatLineComment(
    HiddenNodeFormatter& comment) const {
  comment.replace(cursiv::AbstractFormatter::formatLineComment(comment));
}

WorkspaceFormatter::WorkspaceFormatter(const cursiv::CoreServices& services)
    : AbstractFormatter(services) {
  on<ast::Workspace>(&WorkspaceFormatter::formatWorkspace);
  on<ast::PackageDeclaration>(&WorkspaceFormatter::formatPackageDeclaration);
  on<ast::Entity>(&WorkspaceFormatter::formatEntity);
  on<ast::DataType>(&WorkspaceFormatter::formatDataType);
  on<ast::Feature>(&WorkspaceFormatter::formatFeature);
  onHidden("ML_COMMENT", &WorkspaceFormatter::formatMultilineComment);
  onHidden("SL_COMMENT", &WorkspaceFormatter::formatLineComment);
}

}  // namespace ws
