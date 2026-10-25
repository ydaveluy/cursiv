#include "workspace/lsp/WorkspaceRenameProvider.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "workspace/core/CoreServices.hpp"
#include "workspace/core/ast.hpp"
#include "workspace/core/references/QualifiedNameProvider.hpp"

namespace ws {

namespace {

// The name is read through the tag, not a cast: `ast_cast` is exact, so an
// `Entity` would not see itself as the `AbstractElement` that names it.
std::string_view name_of(const cursiv::AstNode& node) {
  return cursiv::is_named(node) ? node.type->readName(node)
                                : std::string_view{};
}

// The qualified name a declaration will answer to, with `renamed` spelled
// `newName` -- functional rather than mutate-then-read, since an AST is not
// written to answer a request.
std::string qualified_after_rename(const cursiv::AstNode& node,
                                   const cursiv::AstNode& renamed,
                                   std::string_view newName,
                                   const QualifiedNameProvider& names) {
  const std::string own(&node == &renamed ? newName : name_of(node));
  const auto* const package =
      cursiv::ast_cast<const ast::PackageDeclaration>(node.parent);
  if (package == nullptr) return own;
  return names.getQualifiedName(
      qualified_after_rename(*package, renamed, newName, names), own);
}

// The renamed declaration and everything under it: a package carries its
// members' qualified names, so each of them has references to rewrite too.
void collect_subtree(const cursiv::AstNode& node,
                     std::vector<const cursiv::AstNode*>& out) {
  out.push_back(&node);
  if (const auto* const package =
          cursiv::ast_cast<const ast::PackageDeclaration>(&node))
    for (const ast::AbstractElement* const element : package->elements)
      if (element != nullptr) collect_subtree(*element, out);
}

}  // namespace

WorkspaceRenameProvider::WorkspaceRenameProvider(
    const WorkspaceCoreServices& services) noexcept
    : cursiv::DefaultRenameProvider(services), _dm(services) {}

std::optional<lsp::WorkspaceEdit> WorkspaceRenameProvider::rename(
    const cursiv::Document& document, lsp::Position position,
    std::string_view newName, const cursiv::CancellationToken& token) const {
  const QualifiedNameProvider& names = *_dm.qualifiedNameProvider;
  const cursiv::References& references = *core().references;
  const cursiv::Documents& store = *core().shared.documents;

  // The declaration edit is the base's: what a new name may be, and where a
  // declaration writes it, is not this language's business.
  std::optional<lsp::WorkspaceEdit> edit =
      cursiv::DefaultRenameProvider::rename(document, position, newName, token);
  if (!edit.has_value()) return edit;

  const cursiv::Declaration found = references.findDeclaration(
      document, document.textDocument().offsetAt(cursiv::from_lsp(position)),
      token);
  if (!found || found.targets.empty()) return edit;

  std::vector<const cursiv::AstNode*> renamed;
  collect_subtree(*found.targets.front(), renamed);

  // Once, and never inside the loop: `emplace` on an optional constructs a
  // new value, so calling it per reference would throw away the declaration
  // edits the base just made.
  if (!edit->changes.has_value()) edit->changes.emplace();
  auto& changes = *edit->changes;

  // A site written under a qualifier takes the new qualified name; a bare one
  // takes the bare name. Which of the two a site is, is what it spells.
  for (const cursiv::AstNode* const node : renamed) {
    cursiv::throw_if_cancelled(token);
    const std::string qualified =
        qualified_after_rename(*node, *found.targets.front(), newName, names);
    const std::string bare = node == found.targets.front()
                                 ? std::string(newName)
                                 : std::string(name_of(*node));

    for (const cursiv::ReferenceDescription& reference :
         references.findReferences(*node, {false, cursiv::kNoDocument},
                                   token)) {
      const std::shared_ptr<cursiv::Document> source =
          store.getDocument(reference.sourceDocumentId);
      if (source == nullptr) continue;
      const cursiv::TextDocument& text = source->textDocument();
      if (reference.sourceEnd > text.getText().size()) continue;
      const std::string_view written = text.getText().substr(
          reference.sourceBegin, reference.sourceEnd - reference.sourceBegin);

      // A bare site takes the referenced node's own name, which is `newName`
      // exactly when that node is the one renamed -- never the qualified
      // form, even when an ancestor package is what was renamed.
      const std::string_view replacement =
          written.find('.') == std::string_view::npos ? bare : qualified;
      if (written == replacement) continue;  // nothing to say

      lsp::TextEdit rewrite;
      rewrite.range = cursiv::to_lsp(
          text.rangeAt(reference.sourceBegin, reference.sourceEnd));
      rewrite.newText = std::string(replacement);

      // The base already wrote the bare name at this site; ours is the one
      // that knows whether the site is qualified, so it replaces rather than
      // adds -- two edits over one span is a rename a client refuses.
      auto& rows = changes[lsp::DocumentUri::parse(std::string(source->uri))];
      const auto at =
          std::ranges::find_if(rows, [&rewrite](const lsp::TextEdit& other) {
            return other.range.start.line == rewrite.range.start.line &&
                   other.range.start.character == rewrite.range.start.character;
          });
      if (at != rows.end())
        at->newText = std::move(rewrite.newText);
      else
        rows.push_back(std::move(rewrite));
    }
  }
  return edit;
}

}  // namespace ws
