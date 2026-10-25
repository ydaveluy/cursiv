#include "domainmodel/core/references/DomainModelScopeComputation.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/grammar/Conversion.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/workspace/AstNodeDescriptionProvider.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "domainmodel/core/CoreServices.hpp"
#include "domainmodel/core/references/QualifiedNameProvider.hpp"

namespace dm {

namespace {

// The family is asked once, not per node: the erased `isInstance` scans the
// grammar's classes to find the tag it was handed, and a scope computation
// visits every node of the document.
bool in_family(std::span<const cursiv::AstTypeTag* const> family,
               const cursiv::AstTypeTag* tag) noexcept {
  return std::ranges::find(family, tag) != family.end();
}

// A computed name lives in the document's pool, never on this stack: a
// description borrows its name, and the pool is freed by the same reset that
// purges the index rows pointing into it.
std::string_view keep(const cursiv::Document& document,
                      const std::string& name) {
  return cursiv::copy_into(document.parseResult.cst().memory(), name);
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
  for (cursiv::AstNode* const node : document.parseResult.nodes) {
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
  const std::string_view qualified = keep(
      document,
      _dm.qualifiedNameProvider->getQualifiedName(document, *package, *own));
  if (auto description = _dm.astNodeDescriptionProvider->createDescription(
          node, qualified, &document))
    exported.push_back(*std::move(description));
}

// The other direction: the walk descends, and a nested package requalifies what
// its children returned -- so a name is spelled at every storey it is visible
// from, and `foo.bar.E2` resolves from the root as `E2` does from inside.
cursiv::LocalSymbols DomainModelScopeComputation::collectLocalSymbols(
    const cursiv::Document& document,
    const cursiv::CancellationToken& cancelToken) const {
  cursiv::LocalSymbols symbols;
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr || root->type != &cursiv::ast_type_tag<ast::DomainModel>)
    return symbols;

  symbols.reserve(document.parseResult.nodes.size() / 4);
  (void)processContainer(*root,
                         static_cast<const ast::DomainModel&>(*root).elements,
                         document, symbols, cancelToken);
  symbols.seal();
  return symbols;
}

std::vector<cursiv::AstNodeDescription>
DomainModelScopeComputation::processContainer(
    const cursiv::AstNode& container,
    std::span<ast::AbstractElement* const> elements,
    const cursiv::Document& document, cursiv::LocalSymbols& symbols,
    const cursiv::CancellationToken& cancelToken) const {
  const QualifiedNameProvider& names = *_dm.qualifiedNameProvider;
  const std::span<const cursiv::AstTypeTag* const> types =
      _dm.astReflection->getAllSubTypes(&cursiv::ast_type_tag<ast::Type>);

  std::vector<cursiv::AstNodeDescription> declared;
  declared.reserve(elements.size());

  for (ast::AbstractElement* const element : elements) {
    cursiv::throw_if_cancelled(cancelToken);
    if (element == nullptr) continue;

    if (in_family(types, element->type)) {
      if (element->name.empty()) continue;
      if (auto description = _dm.astNodeDescriptionProvider->createDescription(
              *element, element->name, &document))
        declared.push_back(*std::move(description));
      continue;
    }
    if (element->type != &cursiv::ast_type_tag<ast::PackageDeclaration>)
      continue;

    const auto& package = static_cast<const ast::PackageDeclaration&>(*element);
    for (cursiv::AstNodeDescription& nested : processContainer(
             package, package.elements, document, symbols, cancelToken)) {
      nested.name =
          keep(document, names.getQualifiedName(package.name, nested.name));
      declared.push_back(std::move(nested));
    }
  }

  for (const cursiv::AstNodeDescription& description : declared)
    symbols.add(&container, description);
  return declared;
}

}  // namespace dm
