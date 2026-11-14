#include "workspace/core/references/WorkspaceScopeComputation.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/grammar/Conversion.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/workspace/AstNodeDescriptionProvider.hpp"
#include "cursiv/workspace/Document.hpp"
#include "workspace/core/CoreServices.hpp"
#include "workspace/core/references/QualifiedNameProvider.hpp"

namespace ws {

namespace {

// Searches subtypes fetched once per document: the erased `isInstance` scans
// the grammar's classes on every call.
bool in_family(std::span<const cursiv::AstTypeTag* const> family,
               const cursiv::AstTypeTag* tag) noexcept {
  return std::ranges::find(family, tag) != family.end();
}

// A description borrows its name, so a computed name lives in the document's
// pool, freed by the same reset that drops the index rows pointing into it.
std::string_view keep(const cursiv::Document& document,
                      const std::string& name) {
  return document.keepText(name);
}

}  // namespace

WorkspaceScopeComputation::WorkspaceScopeComputation(
    const WorkspaceCoreServices& services) noexcept
    : cursiv::DefaultScopeComputation(services), _dm(services) {}

// Every `Type` of the document, however deep it nests, not only the root's
// children; `addExportedSymbol` chooses the name.
std::vector<cursiv::AstNodeDescription>
WorkspaceScopeComputation::collectExportedSymbols(
    const cursiv::Document& document,
    const cursiv::CancellationToken& cancelToken) const {
  std::vector<cursiv::AstNodeDescription> exported;
  if (document.parseResult.value == nullptr) return exported;

  const std::span<const cursiv::AstTypeTag* const> types =
      _dm.astReflection->getAllSubTypes(&cursiv::ast_type_tag<ast::Type>);
  for (cursiv::AstNode* const node : document.parseResult.nodes()) {
    cursiv::throw_if_cancelled(cancelToken);
    if (in_family(types, node->type))
      addExportedSymbol(*node, exported, document);
  }
  return exported;
}

void WorkspaceScopeComputation::addExportedSymbol(
    cursiv::AstNode& node, std::vector<cursiv::AstNodeDescription>& exported,
    const cursiv::Document& document) const {
  const auto* const package =
      cursiv::ast_cast<const ast::PackageDeclaration>(node.parent);
  if (package == nullptr) {
    cursiv::DefaultScopeComputation::addExportedSymbol(node, exported,
                                                       document);
    return;
  }
  const std::optional<std::string_view> own =
      services.nameProvider->getName(node);
  if (!own.has_value() || own->empty()) return;
  const std::string_view qualified = keep(
      document,
      _dm.qualifiedNameProvider->getQualifiedName(document, *package, *own));
  if (auto description = _dm.astNodeDescriptionProvider->createDescription(
          node, qualified, &document))
    exported.push_back(*std::move(description));
}

// A declaration is visible in its package by its bare name, and in each
// enclosing package qualified by the packages between: `foo.bar.E2` resolves
// from the root as `E2` does inside `foo.bar`.
cursiv::LocalSymbols WorkspaceScopeComputation::collectLocalSymbols(
    const cursiv::Document& document,
    const cursiv::CancellationToken& cancelToken) const {
  cursiv::LocalSymbols symbols;
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::Workspace>)
    return symbols;

  symbols.reserve(document.parseResult.nodes().size() / 4);
  processContainer(*root, static_cast<const ast::Workspace&>(*root).elements,
                   document, symbols, cancelToken);
  symbols.buildIndex();
  return symbols;
}

void WorkspaceScopeComputation::processContainer(
    const cursiv::AstNode& root,
    std::span<ast::AbstractElement* const> elements,
    const cursiv::Document& document, cursiv::LocalSymbols& symbols,
    const cursiv::CancellationToken& cancelToken) const {
  const std::span<const cursiv::AstTypeTag* const> types =
      _dm.astReflection->getAllSubTypes(&cursiv::ast_type_tag<ast::Type>);

  // One entry per open container, so deep nesting uses no native stack. Each
  // collects its declarations in element order, nested packages' names in
  // their place.
  struct Level {
    const cursiv::AstNode* container;
    std::span<ast::AbstractElement* const> elements;
    std::size_t next = 0;
    std::vector<cursiv::AstNodeDescription> declared;
    // The size of `qualifier` while this container is open.
    std::size_t qualified = 0;
  };
  // The open packages, outermost first, each followed by a dot. A type's full
  // qualified name is kept once, and each level names it by a suffix of that
  // string (`E`, `q.E`, `p.q.E`), so deep nesting copies nothing.
  std::string qualifier;
  std::vector<Level> open;
  open.push_back({&root, elements, 0, {}, 0});
  while (!open.empty()) {
    if (Level& level = open.back(); level.next < level.elements.size()) {
      ast::AbstractElement* const element = level.elements[level.next++];
      cursiv::throw_if_cancelled(cancelToken);
      if (element == nullptr) continue;

      if (in_family(types, element->type)) {
        if (element->name.empty()) continue;
        const std::string_view name =
            qualifier.empty() ? element->name
                              : document.keepText(qualifier, element->name)
                                    .substr(qualifier.size());
        if (auto description =
                _dm.astNodeDescriptionProvider->createDescription(
                    *element, name, &document))
          level.declared.push_back(*std::move(description));
        continue;
      }
      if (element->type == &cursiv::ast_type_tag<ast::PackageDeclaration>) {
        const auto& package =
            static_cast<const ast::PackageDeclaration&>(*element);
        open.push_back({&package, package.elements, 0, {}, qualifier.size()});
        // A package whose name is missing qualifies nothing.
        if (!package.name.empty()) {
          qualifier += package.name;
          qualifier += '.';
        }
      }
      continue;
    }

    // A container closes: its names are visible in it, and the enclosing
    // level sees them prefixed by the package name and dot, which sit just
    // before them in the same string.
    Level done = std::move(open.back());
    open.pop_back();
    for (const cursiv::AstNodeDescription& description : done.declared)
      symbols.add(done.container, description);
    if (open.empty()) break;
    // The size of the package name and dot in `qualifier`.
    const std::size_t package = qualifier.size() - done.qualified;
    qualifier.resize(done.qualified);
    for (cursiv::AstNodeDescription& nested : done.declared) {
      nested.name = std::string_view(nested.name.data() - package,
                                     nested.name.size() + package);
      open.back().declared.push_back(std::move(nested));
    }
  }
}

}  // namespace ws
