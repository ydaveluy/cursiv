#include <domainmodel/lsp/DomainModelRenameProvider.hpp>

#include <domainmodel/core/ast.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>

#include <domainmodel/core/references/QualifiedNameProvider.hpp>

#include <pegium/lsp/support/LspProviderUtils.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/references/NameProvider.hpp>
#include <pegium/core/references/References.hpp>
#include <pegium/core/syntax-tree/AstUtils.hpp>
#include <pegium/core/syntax-tree/CstUtils.hpp>
#include <pegium/core/utils/TransparentStringHash.hpp>
#include <pegium/core/workspace/Documents.hpp>

namespace domainmodel {
namespace {

using namespace domainmodel::ast;
using namespace pegium::provider_detail;

const ast::PackageDeclaration *parent_package(const pegium::AstNode &node) {
  return pegium::ast_ptr_cast<const ast::PackageDeclaration>(node.getContainer());
}

std::string package_qualified_name(
    const ast::PackageDeclaration &package, const pegium::AstNode &renamedRoot,
    std::string_view replacementName,
    const domainmodel::references::QualifiedNameProvider *qualifiedNameProvider) {
  const auto currentName =
      &package == &renamedRoot ? std::string(replacementName) : package.name;
  const auto *parent = parent_package(package);
  if (parent == nullptr) {
    return currentName;
  }

  const auto qualifier = package_qualified_name(*parent, renamedRoot, replacementName,
                                                qualifiedNameProvider);
  if (qualifiedNameProvider != nullptr) {
    return qualifiedNameProvider->getQualifiedName(qualifier, currentName);
  }
  return qualifier.empty() ? currentName : qualifier + "." + currentName;
}

bool has_qualified_name_text(const pegium::workspace::Documents &documents,
                             const pegium::workspace::ReferenceDescription &reference) {
  const auto document = documents.getDocument(reference.sourceDocumentId);
  if (document == nullptr) {
    return false;
  }
  return reference.sourceText(document->textDocument().getText()).find('.') !=
         std::string_view::npos;
}

void add_edit(WorkspaceEditData &editData,
              pegium::utils::TransparentStringSet &seen,
              pegium::workspace::DocumentId documentId, pegium::TextOffset begin,
              pegium::TextOffset end, std::string newText) {
  const auto key = location_key(
      {.documentId = documentId, .begin = begin, .end = end});
  if (!seen.insert(key).second) {
    return;
  }
  editData.changes[documentId].push_back(
      {.begin = begin, .end = end, .newText = std::move(newText)});
}

void sort_edits(WorkspaceEditData &editData) {
  for (auto &[documentId, edits] : editData.changes) {
    (void)documentId;
    std::ranges::sort(edits, [](const WorkspaceTextEdit &left,
                                const WorkspaceTextEdit &right) {
      return left.begin > right.begin;
    });
  }
}

std::optional<pegium::CstNodeView>
declaration_name_node(const pegium::AstNode &node,
                      const pegium::references::NameProvider &nameProvider) {
  return nameProvider.getNameNode(node);
}

template <typename F>
void visit_subtree(const pegium::AstNode &root, F &&visitor) {
  visitor(root);
  for (const auto *node : root.getAllContent()) {
    visitor(*node);
  }
}

} // namespace

std::optional<::lsp::WorkspaceEdit> DomainModelRenameProvider::rename(
    const pegium::workspace::Document &document, const ::lsp::RenameParams &params,
    const pegium::utils::CancellationToken &cancelToken) const {
  pegium::utils::throw_if_cancelled(cancelToken);
  const auto newName = std::string_view(params.newName);
  const auto &referencesService = *services.references.references;
  const auto &nameProvider = *services.references.nameProvider;
  const auto &documents = *services.shared.workspace.documents;
  if (newName.empty()) {
    return std::nullopt;
  }
  assert(document.parseResult.cst != nullptr);

  const auto offset = document.textDocument().offsetAt(params.position);
  const auto selectedNode =
      find_declaration_node_at_offset(*document.parseResult.cst, offset);
  if (!selectedNode.has_value()) {
    return std::nullopt;
  }

  const auto targetNodes = referencesService.findDeclarations(*selectedNode);
  if (targetNodes.empty()) {
    return std::nullopt;
  }

  WorkspaceEditData editData;
  pegium::utils::TransparentStringSet seen;
  const auto *qualifiedNameProvider = services.qualifiedNameProvider.get();

  for (const auto *target : targetNodes) {
    pegium::utils::throw_if_cancelled(cancelToken);
    const auto &targetDocument = pegium::getDocument(*target);
    if (auto nameNode = declaration_name_node(*target, nameProvider);
        nameNode.has_value()) {
      add_edit(editData, seen, targetDocument.id, nameNode->getBegin(),
               nameNode->getEnd(), std::string(newName));
    }
  }

  const auto &renamedRoot = *targetNodes.front();
  visit_subtree(renamedRoot, [&](const pegium::AstNode &node) {
    pegium::utils::throw_if_cancelled(cancelToken);
    const auto qualifiedName =
        buildQualifiedName(node, renamedRoot, newName, qualifiedNameProvider);
    if (!qualifiedName.has_value()) {
      return;
    }
    for (const auto &reference :
         referencesService.findReferences(node, {.includeDeclaration = false})) {
      pegium::utils::throw_if_cancelled(cancelToken);
      const auto replacement =
          has_qualified_name_text(documents, reference)
              ? *qualifiedName
              : std::string(newName);
      add_edit(editData, seen, reference.sourceDocumentId,
               reference.sourceOffset,
               static_cast<pegium::TextOffset>(reference.sourceOffset +
                                               reference.sourceLength),
               replacement);
    }
  });

  if (editData.empty()) {
    return std::nullopt;
  }

  sort_edits(editData);
  return to_lsp_workspace_edit(editData, services.shared, cancelToken);
}

std::optional<std::string> DomainModelRenameProvider::buildQualifiedName(
    const pegium::AstNode &node, const pegium::AstNode &renamedRoot,
    std::string_view replacementName,
    const domainmodel::references::QualifiedNameProvider *qualifiedNameProvider) const {
  if (services.shared.astReflection->isInstance(
          node, std::type_index(typeid(Feature)))) {
    return std::nullopt;
  }

  const auto &nameProvider = *services.references.nameProvider;

  auto name = nameProvider.getName(node);
  if (!name.has_value()) {
    return std::nullopt;
  }

  auto resolvedName = &node == &renamedRoot ? std::string(replacementName)
                                            : std::move(*name);
  const auto *container = parent_package(node);
  if (container == nullptr) {
    return resolvedName;
  }

  const auto qualifier = package_qualified_name(*container, renamedRoot,
                                                replacementName, qualifiedNameProvider);
  if (qualifiedNameProvider != nullptr) {
    return qualifiedNameProvider->getQualifiedName(qualifier, resolvedName);
  }
  return qualifier.empty() ? resolvedName : qualifier + "." + resolvedName;
}

} // namespace domainmodel
