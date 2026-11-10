#include "domainmodel/references/DomainModelScopeComputation.hpp"

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
#include "domainmodel/CoreServices.hpp"
#include "domainmodel/references/QualifiedNameProvider.hpp"

namespace dm {

namespace {

// The family is asked once, not per node: the erased `isInstance` scans the
// grammar's classes to find the tag it was handed, and a scope computation
// visits every node of the document.
bool in_family(std::span<const cursiv::AstTypeTag* const> family,
               const cursiv::AstTypeTag* tag) noexcept {
  return std::ranges::find(family, tag) != family.end();
}

}  // namespace

DomainModelScopeComputation::DomainModelScopeComputation(
    const DomainModelCoreServices& services) noexcept
    : cursiv::DefaultScopeComputation(services), _dm(services) {}

// Every `Type` of the document, wherever it nests: the exported slice is the
// Type family, not the root's direct children -- so the walk is this one, and
// only what a member exports as goes through the hook.
std::vector<cursiv::AstNodeDescription>
DomainModelScopeComputation::collectExportedSymbols(
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

void DomainModelScopeComputation::addExportedSymbol(
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
  const std::string_view qualified =
      _dm.qualifiedNameProvider->keepQualifiedName(document, *package, *own);
  if (auto description = _dm.astNodeDescriptionProvider->createDescription(
          node, qualified, &document))
    exported.push_back(*std::move(description));
}

// The other direction: the walk descends, and a nested package requalifies what
// its children declared -- so a name is spelled at every level it is visible
// from, and `foo.bar.E2` resolves from the root as `E2` does from inside.
cursiv::LocalSymbols DomainModelScopeComputation::collectLocalSymbols(
    const cursiv::Document& document,
    const cursiv::CancellationToken& cancelToken) const {
  cursiv::LocalSymbols symbols;
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::DomainModel>)
    return symbols;

  symbols.reserve(document.parseResult.nodes().size() / 4);
  processContainer(*root, static_cast<const ast::DomainModel&>(*root).elements,
                   document, symbols, cancelToken);
  symbols.build_index();
  return symbols;
}

void DomainModelScopeComputation::processContainer(
    const cursiv::AstNode& root,
    std::span<ast::AbstractElement* const> elements,
    const cursiv::Document& document, cursiv::LocalSymbols& symbols,
    const cursiv::CancellationToken& cancelToken) const {
  const std::span<const cursiv::AstTypeTag* const> types =
      _dm.astReflection->getAllSubTypes(&cursiv::ast_type_tag<ast::Type>);

  // One `Level` per open container, so packages nested a thousand deep hold
  // no native frame each. Each collects what it declares in element order,
  // its nested packages' names at their places.
  struct Level {
    const cursiv::AstNode* container;
    std::span<ast::AbstractElement* const> elements;
    std::size_t next = 0;
    std::vector<cursiv::AstNodeDescription> declared;
    // How long `qualifier` is while this level is open.
    std::size_t qualified = 0;
  };
  // The open packages' names, outermost first, each with its dot. A nested
  // type's qualified name is kept once, and each level names it by a suffix:
  // `E`, then `q.E`, then `p.q.E`.
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
                              : document.keep_text(qualifier, element->name)
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

    // A level closes: its names are known there, and gain its package's name
    // and dot for the enclosing level.
    Level done = std::move(open.back());
    open.pop_back();
    for (const cursiv::AstNodeDescription& description : done.declared)
      symbols.add(done.container, description);
    if (open.empty()) break;
    // The width the package's name and dot added to `qualifier`.
    const std::size_t package = qualifier.size() - done.qualified;
    qualifier.resize(done.qualified);
    for (cursiv::AstNodeDescription& nested : done.declared) {
      nested.name = std::string_view(nested.name.data() - package,
                                     nested.name.size() + package);
      open.back().declared.push_back(std::move(nested));
    }
  }
}

}  // namespace dm
